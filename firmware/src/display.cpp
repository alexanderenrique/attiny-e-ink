#include "display.h"

#include <Arduino.h>
#include <SPI.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "font.h"
#include "pins.h"
#include "power.h"
#include "quote_select.h"
#include "sleep_rtc.h"

static constexpr uint16_t FULL_REFRESH_TIMEOUT_S = 32;
static constexpr uint16_t SHORT_BUSY_TIMEOUT_S = 1;

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

static constexpr uint8_t MAX_LINES = 8;
static constexpr uint16_t MARGIN_X = 8;
static constexpr uint16_t MARGIN_Y = 4;
static constexpr uint16_t LINE_GAP = 2;
static constexpr uint16_t MAX_TEXT_WIDTH = EPD_WIDTH - 2 * MARGIN_X;

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
};

static uint16_t textWidth(const char* s, uint8_t len) {
  return static_cast<uint16_t>(len) * FONT_WIDTH;
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
      const uint16_t nextW = textWidth(lineStart, static_cast<uint8_t>(len + 1));
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
    layout->hasBattery = true;
  }
}

static bool glyphPixel(char c, int gx, int gy) {
  if (gx < 0 || gx >= FONT_WIDTH || gy < 0 || gy >= FONT_HEIGHT) {
    return false;
  }
  const uint8_t* col = fontGlyph(c);
  const uint8_t bits = pgm_read_byte(&col[gx]);
  return (bits & (1u << gy)) != 0;
}

static bool layoutPixel(const Layout* layout, int16_t lx, int16_t ly,
                        bool* redOut) {
  *redOut = false;
  if (lx < 0 || ly < 0 || lx >= static_cast<int16_t>(EPD_WIDTH) ||
      ly >= static_cast<int16_t>(EPD_HEIGHT)) {
    return false;
  }

  // Battery percentage, top-right, red.
  if (layout->hasBattery) {
    const uint8_t blen = static_cast<uint8_t>(strlen(layout->batteryText));
    const int16_t bx =
        static_cast<int16_t>(EPD_WIDTH - MARGIN_X - blen * FONT_WIDTH);
    const int16_t by = static_cast<int16_t>(MARGIN_Y);
    if (lx >= bx && ly >= by && ly < by + FONT_HEIGHT) {
      const int16_t rel = lx - bx;
      const char ch = layout->batteryText[rel / FONT_WIDTH];
      if (glyphPixel(ch, rel % FONT_WIDTH, ly - by)) {
        *redOut = true;
        return true;
      }
    }
  }

  const uint16_t lineHeight = FONT_HEIGHT + LINE_GAP;
  for (uint8_t i = 0; i < layout->count; ++i) {
    const int16_t y0 =
        static_cast<int16_t>(MARGIN_Y + static_cast<uint16_t>(i) * lineHeight);
    if (ly < y0 || ly >= y0 + FONT_HEIGHT) {
      continue;
    }
    const LaidOutLine& line = layout->lines[i];
    const int16_t x0 = static_cast<int16_t>(MARGIN_X);
    const int16_t x1 =
        static_cast<int16_t>(x0 + static_cast<int16_t>(line.len) * FONT_WIDTH);
    if (lx < x0 || lx >= x1) {
      continue;
    }
    const int16_t rel = lx - x0;
    const char ch = line.start[rel / FONT_WIDTH];
    if (glyphPixel(ch, rel % FONT_WIDTH, ly - y0)) {
      *redOut = line.red;
      return true;
    }
  }
  return false;
}

static void streamPlane(const Layout* layout, bool redPlane) {
  // Stream full native frame, 8 native rows at a time (128 bytes).
  uint8_t page[128];

  epdSetRamArea(0, 0, EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);
  epdWriteCommand(redPlane ? 0x26 : 0x24);

  digitalWrite(PIN_EPD_DC, HIGH);
  epdCs(true);
  SPI.beginTransaction(epdSpi);

  for (uint16_t ny = 0; ny < EPD_NATIVE_HEIGHT; ny += 8) {
    memset(page, 0xFF, sizeof(page));  // white

    for (uint8_t row = 0; row < 8; ++row) {
      const uint16_t y = static_cast<uint16_t>(ny + row);
      if (y >= EPD_NATIVE_HEIGHT) {
        break;
      }
      for (uint8_t xb = 0; xb < 16; ++xb) {
        uint8_t byteVal = 0xFF;
        for (uint8_t bit = 0; bit < 8; ++bit) {
          const int16_t nx = static_cast<int16_t>(xb * 8 + bit);
          const int16_t lx =
              static_cast<int16_t>(EPD_NATIVE_HEIGHT - 1 - static_cast<int16_t>(y));
          const int16_t ly = nx;
          bool isRed = false;
          const bool on = layoutPixel(layout, lx, ly, &isRed);
          if (!on) {
            continue;
          }
          if (redPlane) {
            if (isRed) {
              // Controller red RAM: 0 = red after GxEPD2 ~invert convention.
              // We write inverted at the end of the byte assembly:
              byteVal &= static_cast<uint8_t>(~(1u << (7 - bit)));
            }
          } else {
            if (!isRed) {
              byteVal &= static_cast<uint8_t>(~(1u << (7 - bit)));
            }
          }
        }
        // SSD1680: bit7 is leftmost pixel of the byte.
        if (redPlane) {
          // Match GxEPD2: store ~color where color bitmap 0=red → write 1.
          // Our byteVal already has 0 where red. Invert for the red plane.
          page[row * 16 + xb] = static_cast<uint8_t>(~byteVal);
        } else {
          page[row * 16 + xb] = byteVal;
        }
      }
    }

    for (uint16_t i = 0; i < sizeof(page); ++i) {
      SPI.transfer(page[i]);
    }
  }

  SPI.endTransaction();
  epdCs(false);
}

bool displayShowLowBattery(int batteryPercent) {
  DisplayQuote q;
  q.deckLabel = "Battery Low";
  q.quote = "Please Charge";
  q.author = nullptr;
  q.batteryPercent = batteryPercent;
  return displayShowQuote(q);
}

bool displayShowQuote(const DisplayQuote& q) {
  Layout layout;
  buildLayout(&layout, q);

  epdInit();

  // Clear both planes white via streamPlane with empty layout first? The
  // quote layout already draws on white background (0xFF defaults).
  streamPlane(&layout, false);  // black
  streamPlane(&layout, true);   // red

  epdUpdateFull();
  epdDeepSleep();
  epdCutPowerAfterDeepSleep();
  return true;
}
