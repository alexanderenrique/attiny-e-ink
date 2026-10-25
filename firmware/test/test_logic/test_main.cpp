#include <unity.h>

#include <string.h>

#include "battery.h"
#include "config_parse.h"
#include "display_raster.h"
#include "font.h"
#include "pit_interval.h"
#include "quote_select.h"

void setUp(void) {}
void tearDown(void) {}

void test_config_defaults_and_interval(void) {
  QuoteConfig cfg;
  TEST_ASSERT_EQUAL_UINT16(7, cfg.intervalHours);
  TEST_ASSERT_EQUAL(QuoteOrder::Sequential, cfg.order);
  TEST_ASSERT_EQUAL_STRING("*", cfg.deck);

  TEST_ASSERT_TRUE(configApplyLine(&cfg, "interval=15"));
  TEST_ASSERT_EQUAL_UINT16(15, cfg.intervalHours);

  TEST_ASSERT_TRUE(configApplyLine(&cfg, "  interval = 90  "));
  TEST_ASSERT_EQUAL_UINT16(kMaxIntervalHours, cfg.intervalHours);

  TEST_ASSERT_FALSE(configApplyLine(&cfg, "interval=abc"));
  TEST_ASSERT_EQUAL_UINT16(kMaxIntervalHours, cfg.intervalHours);
}

void test_config_order_and_deck(void) {
  QuoteConfig cfg;
  TEST_ASSERT_TRUE(configApplyLine(&cfg, "order=random"));
  TEST_ASSERT_EQUAL(QuoteOrder::Random, cfg.order);
  TEST_ASSERT_TRUE(configApplyLine(&cfg, "order=sequential"));
  TEST_ASSERT_EQUAL(QuoteOrder::Sequential, cfg.order);

  TEST_ASSERT_TRUE(configApplyLine(&cfg, "deck=stoic.txt"));
  TEST_ASSERT_EQUAL_STRING("stoic.txt", cfg.deck);
  TEST_ASSERT_TRUE(configApplyLine(&cfg, "deck=*"));
  TEST_ASSERT_EQUAL_STRING("*", cfg.deck);
}

void test_config_ignores_comments_and_blank(void) {
  QuoteConfig cfg;
  TEST_ASSERT_FALSE(configApplyLine(&cfg, ""));
  TEST_ASSERT_FALSE(configApplyLine(&cfg, "# interval=1"));
  TEST_ASSERT_FALSE(configApplyLine(&cfg, "   "));
  TEST_ASSERT_EQUAL_UINT16(7, cfg.intervalHours);
}

void test_quote_deck_file_name(void) {
  TEST_ASSERT_TRUE(quoteDeckFileName("facts.txt"));
  TEST_ASSERT_TRUE(quoteDeckFileName("Facts.TXT"));
  TEST_ASSERT_FALSE(quoteDeckFileName("._facts.txt"));
  TEST_ASSERT_FALSE(quoteDeckFileName(".facts.txt"));
  TEST_ASSERT_FALSE(quoteDeckFileName(".DS_Store"));
  TEST_ASSERT_FALSE(quoteDeckFileName("facts.txt.bak"));
  TEST_ASSERT_FALSE(quoteDeckFileName(".txt"));
  TEST_ASSERT_FALSE(quoteDeckFileName(""));
  TEST_ASSERT_FALSE(quoteDeckFileName(nullptr));
}

void test_quote_line_skippable(void) {
  TEST_ASSERT_TRUE(quoteLineIsSkippable(""));
  TEST_ASSERT_TRUE(quoteLineIsSkippable("   "));
  TEST_ASSERT_TRUE(quoteLineIsSkippable("# comment"));
  TEST_ASSERT_TRUE(quoteLineIsSkippable("  # indented"));
  TEST_ASSERT_FALSE(quoteLineIsSkippable("Hello"));
  TEST_ASSERT_FALSE(quoteLineIsSkippable("  Hello"));
}

void test_quote_split_author(void) {
  char buf[80];
  char* quote = nullptr;
  char* author = nullptr;

  strcpy(buf, "Wisdom is knowing. | Socrates");
  quoteSplitAuthor(buf, &quote, &author);
  TEST_ASSERT_EQUAL_STRING("Wisdom is knowing.", quote);
  TEST_ASSERT_EQUAL_STRING("Socrates", author);

  strcpy(buf, "No author here");
  quoteSplitAuthor(buf, &quote, &author);
  TEST_ASSERT_EQUAL_STRING("No author here", quote);
  TEST_ASSERT_EQUAL_STRING("", author);
}

void test_sequential_wrap(void) {
  uint16_t next = 0;
  TEST_ASSERT_EQUAL_UINT16(0, quoteNextSequentialIndex(0, 3, &next));
  TEST_ASSERT_EQUAL_UINT16(1, next);
  TEST_ASSERT_EQUAL_UINT16(1, quoteNextSequentialIndex(1, 3, &next));
  TEST_ASSERT_EQUAL_UINT16(2, next);
  TEST_ASSERT_EQUAL_UINT16(2, quoteNextSequentialIndex(2, 3, &next));
  TEST_ASSERT_EQUAL_UINT16(0, next);
  TEST_ASSERT_EQUAL_UINT16(0, quoteNextSequentialIndex(0, 0, &next));
  TEST_ASSERT_EQUAL_UINT16(0, next);
}

void test_line_scanner_counts_eligible(void) {
  const char* text =
      "# header\n"
      "\n"
      "First quote\n"
      "Second quote | Author\n"
      "# skip me\n"
      "Third\n";

  QuoteLineScanner scanner;
  scanner.feed(text, strlen(text), true);
  TEST_ASSERT_EQUAL_UINT16(3, scanner.eligible);
}

void test_line_scanner_crlf(void) {
  const char* text = "One\r\nTwo\r\n";
  QuoteLineScanner scanner;
  scanner.feed(text, strlen(text), true);
  TEST_ASSERT_EQUAL_UINT16(2, scanner.eligible);
}

void test_line_picker_nth(void) {
  const char* text =
      "# c\n"
      "Alpha\n"
      "Beta\n"
      "Gamma\n";

  char out[64];
  QuoteLinePicker picker;

  picker.begin(0, out, sizeof(out));
  picker.feed(text, strlen(text), true);
  TEST_ASSERT_TRUE(picker.done);
  TEST_ASSERT_EQUAL_STRING("Alpha", out);

  picker.begin(1, out, sizeof(out));
  picker.feed(text, strlen(text), true);
  TEST_ASSERT_TRUE(picker.done);
  TEST_ASSERT_EQUAL_STRING("Beta", out);

  picker.begin(2, out, sizeof(out));
  picker.feed(text, strlen(text), true);
  TEST_ASSERT_TRUE(picker.done);
  TEST_ASSERT_EQUAL_STRING("Gamma", out);
}

void test_battery_curve_endpoints(void) {
  TEST_ASSERT_EQUAL_INT(0, batteryPercentFromMillivolts(3000));
  TEST_ASSERT_EQUAL_INT(0, batteryPercentFromMillivolts(3450));
  TEST_ASSERT_EQUAL_INT(100, batteryPercentFromMillivolts(4200));
  TEST_ASSERT_EQUAL_INT(100, batteryPercentFromMillivolts(4500));
}

void test_battery_curve_ui_estimate(void) {
  TEST_ASSERT_EQUAL_INT(100, batteryPercentFromMillivolts(4200));
  TEST_ASSERT_EQUAL_INT(80, batteryPercentFromMillivolts(4000));
  TEST_ASSERT_EQUAL_INT(60, batteryPercentFromMillivolts(3850));
  TEST_ASSERT_EQUAL_INT(40, batteryPercentFromMillivolts(3750));
  TEST_ASSERT_EQUAL_INT(20, batteryPercentFromMillivolts(3650));
  TEST_ASSERT_EQUAL_INT(10, batteryPercentFromMillivolts(3550));
  // Same slope below 3.55 V: 5% at 3.50 V, 15% at 3.60 V.
  TEST_ASSERT_EQUAL_INT(5, batteryPercentFromMillivolts(3500));
  TEST_ASSERT_EQUAL_INT(15, batteryPercentFromMillivolts(3600));
  TEST_ASSERT_TRUE(batteryPercentFromMillivolts(3500) <= BATTERY_LOW_PERCENT);
  TEST_ASSERT_TRUE(batteryPercentFromMillivolts(3600) >= BATTERY_RESUME_PERCENT);
}

void test_battery_samples_every_five_when_healthy(void) {
  uint8_t wakes = 0;
  int last = -1;
  int samples = 0;
  bool sampledAt[15] = {};

  for (int i = 0; i < 15; ++i) {
    if (batteryScheduleSample(&wakes, last, false)) {
      sampledAt[i] = true;
      samples++;
      last = 80;
      batteryCommitSample(&wakes);
    }
  }

  TEST_ASSERT_EQUAL_INT(3, samples);
  TEST_ASSERT_TRUE(sampledAt[0]);
  TEST_ASSERT_TRUE(sampledAt[5]);
  TEST_ASSERT_TRUE(sampledAt[10]);
  TEST_ASSERT_FALSE(sampledAt[4]);
  TEST_ASSERT_FALSE(sampledAt[9]);
}

void test_battery_samples_every_wake_near_empty_or_warning(void) {
  uint8_t wakes = 0;
  int samples = 0;
  for (int i = 0; i < 5; ++i) {
    if (batteryScheduleSample(&wakes, 10, false)) {
      samples++;
      batteryCommitSample(&wakes);
    }
  }
  TEST_ASSERT_EQUAL_INT(5, samples);

  wakes = 3;
  samples = 0;
  for (int i = 0; i < 4; ++i) {
    if (batteryScheduleSample(&wakes, 80, true)) {
      samples++;
      batteryCommitSample(&wakes);
    }
  }
  TEST_ASSERT_EQUAL_INT(4, samples);
}

void test_battery_retries_when_sample_is_not_committed(void) {
  uint8_t wakes = 4;
  TEST_ASSERT_TRUE(batteryScheduleSample(&wakes, 40, false));
  TEST_ASSERT_EQUAL_UINT8(4, wakes);
  TEST_ASSERT_TRUE(batteryScheduleSample(&wakes, 40, false));
  batteryCommitSample(&wakes);
  TEST_ASSERT_EQUAL_UINT8(0, wakes);
  TEST_ASSERT_FALSE(batteryScheduleSample(&wakes, 40, false));
}

void test_pit_wakes_for_seconds(void) {
  TEST_ASSERT_EQUAL_UINT32(1, pitWakesForSeconds(0));
  TEST_ASSERT_EQUAL_UINT32(1, pitWakesForSeconds(1));
  TEST_ASSERT_EQUAL_UINT32(1, pitWakesForSeconds(32));
  TEST_ASSERT_EQUAL_UINT32(2, pitWakesForSeconds(33));
  TEST_ASSERT_EQUAL_UINT32(788, pitWakesForHours(7));
}

void test_header_reader_hash_titles(void) {
  char out[64];
  QuoteHeaderReader reader;

  const char* fun = "#Fun Facts\nHoney never spoils.\n";
  reader.begin(out, sizeof(out));
  reader.feed(fun, strlen(fun), true);
  TEST_ASSERT_TRUE(reader.done);
  TEST_ASSERT_TRUE(reader.found);
  TEST_ASSERT_EQUAL_STRING("Fun Facts", out);

  const char* cats = "# Cat Facts!\r\nnext\n";
  reader.begin(out, sizeof(out));
  reader.feed(cats, strlen(cats), true);
  TEST_ASSERT_EQUAL_STRING("Cat Facts!", out);

  const char* spaced = "\n  #  Stoic Quotes  \nThe only true wisdom.\n";
  reader.begin(out, sizeof(out));
  reader.feed(spaced, strlen(spaced), true);
  TEST_ASSERT_EQUAL_STRING("Stoic Quotes", out);

  const char* eof = "#Fun Facts";
  reader.begin(out, sizeof(out));
  reader.feed(eof, strlen(eof), true);
  TEST_ASSERT_TRUE(reader.found);
  TEST_ASSERT_EQUAL_STRING("Fun Facts", out);
}

void test_header_reader_absent(void) {
  char out[64];
  QuoteHeaderReader reader;

  const char* quote = "Just a quote\nSecond\n";
  reader.begin(out, sizeof(out));
  reader.feed(quote, strlen(quote), true);
  TEST_ASSERT_TRUE(reader.done);
  TEST_ASSERT_FALSE(reader.found);
  TEST_ASSERT_EQUAL_STRING("", out);

  const char* emptyHash = "#\nquote\n";
  reader.begin(out, sizeof(out));
  reader.feed(emptyHash, strlen(emptyHash), true);
  TEST_ASSERT_FALSE(reader.found);

  const char* blankHash = "#   \nquote\n";
  reader.begin(out, sizeof(out));
  reader.feed(blankHash, strlen(blankHash), true);
  TEST_ASSERT_FALSE(reader.found);
}

void test_header_reader_chunked_and_ignores_later_comments(void) {
  char out[64];
  QuoteHeaderReader reader;
  reader.begin(out, sizeof(out));
  const char* a = "#Quo";
  const char* b = "tes from the leadership book \"The Servant\"\n";
  const char* c = "# not a header\nListening is a skill.\n";
  reader.feed(a, strlen(a), false);
  TEST_ASSERT_FALSE(reader.done);
  reader.feed(b, strlen(b), false);
  TEST_ASSERT_TRUE(reader.found);
  TEST_ASSERT_EQUAL_STRING("Quotes from the leadership book \"The Servant\"", out);

  reader.feed(c, strlen(c), true);
  TEST_ASSERT_EQUAL_STRING("Quotes from the leadership book \"The Servant\"", out);
}

void test_line_picker_chunked(void) {
  const char* a = "Hel";
  const char* b = "lo wor";
  const char* c = "ld\nNext\n";

  char out[64];
  QuoteLinePicker picker;
  picker.begin(0, out, sizeof(out));
  picker.feed(a, strlen(a), false);
  picker.feed(b, strlen(b), false);
  picker.feed(c, strlen(c), true);
  TEST_ASSERT_TRUE(picker.done);
  TEST_ASSERT_EQUAL_STRING("Hello world", out);
}

static void assertGlyphFitsMetrics(const GfxFont* font) {
  TEST_ASSERT_NOT_NULL(font);
  for (uint16_t ch = font->first; ch <= font->last; ++ch) {
    GfxGlyph glyph;
    memcpy(&glyph, &font->glyphs[ch - font->first], sizeof(glyph));
    TEST_ASSERT_TRUE(glyph.xOffset >= 0);
    const int overhang =
        static_cast<int>(glyph.xOffset) + static_cast<int>(glyph.width) -
        static_cast<int>(glyph.xAdvance);
    TEST_ASSERT_TRUE(overhang <= FONT_RIGHT_OVERHANG);
    TEST_ASSERT_TRUE(glyph.yOffset >= -static_cast<int>(FONT_ASCENT));
    TEST_ASSERT_TRUE(static_cast<int>(glyph.yOffset) +
                         static_cast<int>(glyph.height) <=
                     static_cast<int>(FONT_DESCENT));
  }
}

void test_font_glyphs_fit_line_metrics(void) {
  assertGlyphFitsMetrics(&FONT_SERIF);
  assertGlyphFitsMetrics(&FONT_SERIF_BOLD);
}

static bool probeSpans(const RasterSpan* spans, uint8_t count, int16_t x,
                       int16_t y, bool* red) {
  for (uint8_t i = 0; i < count; ++i) {
    const RasterSpan& span = spans[i];
    if (fontTextPixel(span.font, span.text, span.len, span.originX,
                      span.baseline, x, y)) {
      *red = span.red;
      return true;
    }
  }
  return false;
}

static RasterSpan makeSpan(const GfxFont* font, const char* text, int16_t x,
                           int16_t baseline, bool red) {
  RasterSpan span;
  span.font = font;
  span.text = text;
  span.len = static_cast<uint8_t>(strlen(text));
  span.originX = x;
  span.baseline = baseline;
  span.advance = fontTextWidth(font, text, span.len);
  span.red = red;
  return span;
}

static void assertPagesMatchProbe(const RasterSpan* spans, uint8_t count) {
  uint8_t black[EPD_PAGE_BYTES];
  uint8_t red[EPD_PAGE_BYTES];
  for (uint16_t ny = 0; ny < EPD_NATIVE_HEIGHT; ny += EPD_PAGE_ROWS) {
    rasterFillPage(spans, count, ny, black, red);
    for (uint8_t row = 0; row < EPD_PAGE_ROWS; ++row) {
      const int16_t lx = static_cast<int16_t>(EPD_NATIVE_HEIGHT - 1 - (ny + row));
      for (int16_t ly = 0; ly < static_cast<int16_t>(EPD_HEIGHT); ++ly) {
        bool expectRed = false;
        const bool on = probeSpans(spans, count, lx, ly, &expectRed);
        const int got = static_cast<int>(rasterPageInk(black, red, ny, lx, ly));
        if (!on) {
          TEST_ASSERT_EQUAL_INT(static_cast<int>(RasterInk::White), got);
        } else if (expectRed) {
          TEST_ASSERT_EQUAL_INT(static_cast<int>(RasterInk::Red), got);
        } else {
          TEST_ASSERT_EQUAL_INT(static_cast<int>(RasterInk::Black), got);
        }
      }
    }
  }
}

void test_raster_address_matches_panel_rotation(void) {
  uint16_t index = 0;
  uint8_t mask = 0;

  TEST_ASSERT_TRUE(rasterPageAddress(0, 295, 0, &index, &mask));
  TEST_ASSERT_EQUAL_UINT16(0, index);
  TEST_ASSERT_EQUAL_UINT8(0x80, mask);

  TEST_ASSERT_TRUE(rasterPageAddress(0, 295, 7, &index, &mask));
  TEST_ASSERT_EQUAL_UINT16(0, index);
  TEST_ASSERT_EQUAL_UINT8(0x01, mask);

  TEST_ASSERT_TRUE(rasterPageAddress(0, 288, 0, &index, &mask));
  TEST_ASSERT_EQUAL_UINT16(7 * 16, index);
  TEST_ASSERT_EQUAL_UINT8(0x80, mask);

  TEST_ASSERT_FALSE(rasterPageAddress(0, 287, 0, &index, &mask));

  TEST_ASSERT_TRUE(rasterPageAddress(288, 0, 127, &index, &mask));
  TEST_ASSERT_EQUAL_UINT16(127, index);
  TEST_ASSERT_EQUAL_UINT8(0x01, mask);
}

void test_raster_pages_match_pixel_probe(void) {
  const RasterSpan quote[] = {
      makeSpan(&FONT_SERIF_BOLD, "100%", 250, 16, true),
      makeSpan(&FONT_SERIF_BOLD, "Fun Facts", 8, 16, true),
      makeSpan(&FONT_SERIF, "A glyph can overhang, like g and {.", 8, 38, false),
      makeSpan(&FONT_SERIF, "Right edge", 270, 60, false),
  };
  assertPagesMatchProbe(quote, 4);

  const RasterSpan overlap[] = {
      makeSpan(&FONT_SERIF_BOLD, "Ag{", 20, 40, true),
      makeSpan(&FONT_SERIF, "Ag{", 20, 40, false),
  };
  assertPagesMatchProbe(overlap, 2);

  uint8_t black[EPD_PAGE_BYTES];
  uint8_t red[EPD_PAGE_BYTES];
  rasterFillPage(nullptr, 0, 0, black, red);
  for (uint16_t i = 0; i < EPD_PAGE_BYTES; ++i) {
    TEST_ASSERT_EQUAL_UINT8(0xFF, black[i]);
    TEST_ASSERT_EQUAL_UINT8(0x00, red[i]);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_config_defaults_and_interval);
  RUN_TEST(test_config_order_and_deck);
  RUN_TEST(test_config_ignores_comments_and_blank);
  RUN_TEST(test_quote_deck_file_name);
  RUN_TEST(test_quote_line_skippable);
  RUN_TEST(test_quote_split_author);
  RUN_TEST(test_sequential_wrap);
  RUN_TEST(test_battery_curve_endpoints);
  RUN_TEST(test_battery_curve_ui_estimate);
  RUN_TEST(test_battery_samples_every_five_when_healthy);
  RUN_TEST(test_battery_samples_every_wake_near_empty_or_warning);
  RUN_TEST(test_battery_retries_when_sample_is_not_committed);
  RUN_TEST(test_pit_wakes_for_seconds);
  RUN_TEST(test_line_scanner_counts_eligible);
  RUN_TEST(test_line_scanner_crlf);
  RUN_TEST(test_line_picker_nth);
  RUN_TEST(test_line_picker_chunked);
  RUN_TEST(test_header_reader_hash_titles);
  RUN_TEST(test_header_reader_absent);
  RUN_TEST(test_header_reader_chunked_and_ignores_later_comments);
  RUN_TEST(test_font_glyphs_fit_line_metrics);
  RUN_TEST(test_raster_address_matches_panel_rotation);
  RUN_TEST(test_raster_pages_match_pixel_probe);
  return UNITY_END();
}
