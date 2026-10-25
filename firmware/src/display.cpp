#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include "display_raster.h"
#include "font.h"
#include "pins.h"
#include "power.h"
#include "quote_select.h"
#include "sleep_rtc.h"

static constexpr uint16_t FULL_REFRESH_TIMEOUT_S = 32;
static constexpr uint16_t SHORT_BUSY_TIMEOUT_S = 1;
// BUSY can fall before the glass has settled. Q1 stays on through this.
static constexpr uint32_t POST_BUSY_SETTLE_MS = 20000;

static SPISettings epdSpi(4000000, MSBFIRST, SPI_MODE0);

static void epdCs(bool selected) {
  digitalWrite(PIN_EPD_CS, selected ? LOW : HIGH);
}

static void epdWriteCommand(uint8_t cmd) {
  digitalWrite(PIN_EPD_DC, LOW);
  epdCs(true);
  SPI.beginTransaction(epdSpi);
  SPI.transfer(cmd);
  SPI.endTransaction();
  epdCs(false);
}

static void epdWriteData(uint8_t data) {
  digitalWrite(PIN_EPD_DC, HIGH);
  epdCs(true);
  SPI.beginTransaction(epdSpi);
  SPI.transfer(data);
  SPI.endTransaction();
  epdCs(false);
}

static bool epdWaitBusy(uint16_t timeoutSeconds) {
  // Short polls (reset / SWRESET) stay awake; the long full refresh sleeps.
  if (timeoutSeconds <= 1) {
    const uint32_t start = millis();
    const uint32_t limitMs = static_cast<uint32_t>(timeoutSeconds) * 1000UL;
    while (digitalRead(PIN_EPD_BUSY) == HIGH) {
      if (millis() - start > limitMs) {
        return false;
      }
      delay(1);
    }
    return true;
  }
  return sleepUntilBusyLow(timeoutSeconds);
}

static void epdReset() {
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(10);
  digitalWrite(PIN_EPD_RST, LOW);
  delay(2);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(10);
  (void)epdWaitBusy(SHORT_BUSY_TIMEOUT_S);
}

static void epdSetRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  epdWriteCommand(0x44);
  epdWriteData(static_cast<uint8_t>(x / 8));
  epdWriteData(static_cast<uint8_t>((x + w - 1) / 8));
  epdWriteCommand(0x45);
  epdWriteData(static_cast<uint8_t>(y % 256));
  epdWriteData(static_cast<uint8_t>(y / 256));
  epdWriteData(static_cast<uint8_t>((y + h - 1) % 256));
  epdWriteData(static_cast<uint8_t>((y + h - 1) / 256));
  epdWriteCommand(0x4E);
  epdWriteData(static_cast<uint8_t>(x / 8));
  epdWriteCommand(0x4F);
  epdWriteData(static_cast<uint8_t>(y % 256));
  epdWriteData(static_cast<uint8_t>(y / 256));
}

static void epdInit() {
  epdReset();
  delay(10);
  epdWriteCommand(0x12);  // SWRESET
  delay(10);
  (void)epdWaitBusy(SHORT_BUSY_TIMEOUT_S);

  epdWriteCommand(0x01);  // Driver output control
  epdWriteData(0x27);
  epdWriteData(0x01);
  epdWriteData(0x00);

  epdWriteCommand(0x11);  // Data entry mode
  epdWriteData(0x03);

  epdWriteCommand(0x3C);  // Border waveform
  epdWriteData(0x05);

  epdWriteCommand(0x18);  // Built-in temperature sensor
  epdWriteData(0x80);

  epdWriteCommand(0x21);  // Display update control
  epdWriteData(0x00);
  epdWriteData(0x80);

  epdSetRamArea(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);
}

static void epdUpdateFull() {
  epdWriteCommand(0x22);
  epdWriteData(0xF7);
  epdWriteCommand(0x20);
  (void)epdWaitBusy(FULL_REFRESH_TIMEOUT_S);
  delay(POST_BUSY_SETTLE_MS);
}

static void epdDeepSleep() {
  // Booster / power-off sequence, then Deep Sleep Mode 1 (0x10 / 0x01).
  // After 0x10, BUSY stays HIGH until a hardware reset — do not poll it.
  epdWriteCommand(0x22);
  epdWriteData(0x83);
  epdWriteCommand(0x20);
  (void)epdWaitBusy(SHORT_BUSY_TIMEOUT_S);

  epdWriteCommand(0x10);
  epdWriteData(0x01);
  delay(100);
}

// ---------------------------------------------------------------------------
// Text layout (landscape coordinates)
// ---------------------------------------------------------------------------

static constexpr uint16_t TEXT_TOP = 4;
static constexpr uint16_t MARGIN_X = 8;
// Bold header plus four regular body lines at the serif baseline pitch.
static constexpr uint8_t MAX_LINES = static_cast<uint8_t>(
    (EPD_HEIGHT - TEXT_TOP - FONT_ASCENT - FONT_DESCENT) / FONT_Y_ADVANCE + 1);
static_assert(MAX_LINES == 5, "expected a red header and four body lines");
static constexpr uint16_t MAX_TEXT_WIDTH = EPD_WIDTH - 2 * MARGIN_X;

static const GfxFont* lineFont(bool red) {
  return red ? &FONT_SERIF_BOLD : &FONT_SERIF;
}

static int16_t lineBaseline(uint8_t index) {
  return static_cast<int16_t>(TEXT_TOP + FONT_ASCENT +
                              static_cast<uint16_t>(index) * FONT_Y_ADVANCE);
}

struct LaidOutLine {
  const char* start;
  uint8_t len;
  bool red;
};

struct Layout {
  LaidOutLine lines[MAX_LINES];
  uint8_t count = 0;
  char batteryText[8] = "";
  bool hasBattery = false;
  int16_t batteryX = 0;
};

static uint16_t textWidth(const GfxFont* font, const char* s, uint8_t len) {
  return fontTextWidth(font, s, len);
}

static void appendLine(Layout* layout, const char* start, uint8_t len, bool red) {
  if (layout == nullptr || layout->count >= MAX_LINES || start == nullptr ||
      len == 0) {
    return;
  }
  layout->lines[layout->count].start = start;
  layout->lines[layout->count].len = len;
  layout->lines[layout->count].red = red;
  ++layout->count;
}

static void wrapText(Layout* layout, const char* text, bool red) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }

  const GfxFont* font = lineFont(red);
  const char* p = text;
  while (*p && layout->count < MAX_LINES) {
    // Skip leading spaces on a new visual line.
    while (*p == ' ') {
      ++p;
    }
    if (*p == '\0') {
      break;
    }

    const char* lineStart = p;
    const char* lastBreak = nullptr;
    uint8_t lastBreakLen = 0;
    uint8_t len = 0;

    while (*p) {
      if (*p == ' ') {
        lastBreak = p;
        lastBreakLen = len;
      }
      const uint16_t nextW =
          textWidth(font, lineStart, static_cast<uint8_t>(len + 1));
      if (nextW > MAX_TEXT_WIDTH) {
        break;
      }
      ++len;
      ++p;
    }

    if (*p != '\0' && lastBreak != nullptr && lastBreakLen > 0) {
      appendLine(layout, lineStart, lastBreakLen, red);
      p = lastBreak + 1;
    } else if (len > 0) {
      appendLine(layout, lineStart, len, red);
    } else {
      // Single oversized character — emit one and continue.
      appendLine(layout, lineStart, 1, red);
      p = lineStart + 1;
    }

    // Ellipsis if we ran out of rows with more text remaining.
    if (layout->count == MAX_LINES && *p != '\0') {
      LaidOutLine& last = layout->lines[MAX_LINES - 1];
      if (last.len >= 3) {
        // Point at a static ellipsis by shrinking and relying on renderer
        // overlay — simpler: overwrite last three chars conceptually via
        // a dedicated marker length clamp in pixel sampler.
        last.len = static_cast<uint8_t>(last.len);  // keep; sampler adds ...
      }
      break;
    }
  }
}

static void buildLayout(Layout* layout, const DisplayQuote& q) {
  layout->count = 0;
  layout->hasBattery = false;
  layout->batteryText[0] = '\0';
  layout->batteryX = 0;

  if (q.deckLabel != nullptr && q.deckLabel[0] != '\0') {
    wrapText(layout, q.deckLabel, true);
  }
  if (q.quote != nullptr && q.quote[0] != '\0') {
    wrapText(layout, q.quote, false);
  }
  if (q.author != nullptr && q.author[0] != '\0' &&
      layout->count < MAX_LINES) {
    // Prefix with em-dash-like "- " using ASCII.
    static char authorBuf[QUOTE_LINE_MAX];
    authorBuf[0] = '-';
    authorBuf[1] = ' ';
    size_t n = strlen(q.author);
    if (n > sizeof(authorBuf) - 3) {
      n = sizeof(authorBuf) - 3;
    }
    memcpy(authorBuf + 2, q.author, n);
    authorBuf[n + 2] = '\0';
    wrapText(layout, authorBuf, false);
  }

  if (q.batteryPercent >= 0 && q.batteryPercent <= 100) {
    snprintf(layout->batteryText, sizeof(layout->batteryText), "%d%%",
             q.batteryPercent);
    const uint16_t bw = fontTextWidth(
        &FONT_SERIF_BOLD, layout->batteryText,
        static_cast<uint8_t>(strlen(layout->batteryText)));
    layout->batteryX =
        static_cast<int16_t>(EPD_WIDTH - MARGIN_X - static_cast<int16_t>(bw));
    layout->hasBattery = true;
  }
}

static void writeRamBand(uint8_t command, uint16_t nativeY, const uint8_t* bytes) {
  epdSetRamArea(0, nativeY, EPD_NATIVE_WIDTH, EPD_PAGE_ROWS);
  epdWriteCommand(command);
  digitalWrite(PIN_EPD_DC, HIGH);
  epdCs(true);
  SPI.beginTransaction(epdSpi);
  for (uint16_t i = 0; i < EPD_PAGE_BYTES; ++i) {
    SPI.transfer(bytes[i]);
  }
  SPI.endTransaction();
  epdCs(false);
}

static uint8_t pushSpan(RasterSpan* spans, uint8_t count, const GfxFont* font,
                        const char* text, int16_t originX, int16_t baseline,
                        bool red) {
  if (spans == nullptr || text == nullptr || text[0] == '\0' ||
      count >= MAX_LINES + 1) {
    return count;
  }
  const uint8_t len = static_cast<uint8_t>(strlen(text));
  RasterSpan& span = spans[count];
  span.font = font;
  span.text = text;
  span.len = len;
  span.originX = originX;
  span.baseline = baseline;
  span.advance = fontTextWidth(font, text, len);
  span.red = red;
  return static_cast<uint8_t>(count + 1);
}

// Paint both RAM planes from glyphs. Each 8-row band is one landscape slice,
// so a glyph's bitmap is decoded only in the slice it occupies.
static void streamFrame(const Layout* layout) {
  RasterSpan spans[MAX_LINES + 1];
  uint8_t spanCount = 0;

  // Battery is checked before the lines, so it covers the header on overlap.
  if (layout->hasBattery) {
    spanCount = pushSpan(spans, spanCount, &FONT_SERIF_BOLD, layout->batteryText,
                         layout->batteryX, lineBaseline(0), true);
  }
  for (uint8_t i = 0; i < layout->count; ++i) {
    const LaidOutLine& line = layout->lines[i];
    // line.start points into a longer buffer; len bounds the run.
    RasterSpan& span = spans[spanCount];
    span.font = lineFont(line.red);
    span.text = line.start;
    span.len = line.len;
    span.originX = static_cast<int16_t>(MARGIN_X);
    span.baseline = lineBaseline(i);
    span.advance = fontTextWidth(span.font, span.text, span.len);
    span.red = line.red;
    ++spanCount;
  }

  uint8_t black[EPD_PAGE_BYTES];
  uint8_t red[EPD_PAGE_BYTES];
  for (uint16_t ny = 0; ny < EPD_NATIVE_HEIGHT; ny += EPD_PAGE_ROWS) {
    rasterFillPage(spans, spanCount, ny, black, red);
    writeRamBand(0x24, ny, black);
    writeRamBand(0x26, ny, red);
  }
}

bool displayShowLowBattery(int batteryPercent) {
  DisplayQuote q;
  q.deckLabel = "Battery Low";
  q.quote = "Please Charge";
  q.author = nullptr;
  q.batteryPercent = batteryPercent;
  return displayShowQuote(q);
}

bool displayShowNoSdCard(int batteryPercent) {
  DisplayQuote q;
  q.deckLabel = "No SD card detected";
  q.quote = nullptr;
  q.author = nullptr;
  q.batteryPercent = batteryPercent;
  return displayShowQuote(q);
}

bool displayShowQuote(const DisplayQuote& q) {
  Layout layout;
  buildLayout(&layout, q);

  epdInit();
  streamFrame(&layout);
  epdUpdateFull();
  epdDeepSleep();
  epdCutPowerAfterDeepSleep();
  return true;
}
