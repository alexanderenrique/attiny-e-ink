#include <unity.h>

#include <string.h>

#include "battery.h"
#include "config_parse.h"
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
  return UNITY_END();
}
