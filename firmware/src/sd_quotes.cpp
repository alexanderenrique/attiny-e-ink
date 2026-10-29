#include "sd_quotes.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

#include <SdFat.h>

#include "eeprom_state.h"
#include "pins.h"

static SdFat sd;

static void deckLabelFromName(const char* name, char* out, size_t outCap) {
  if (out == nullptr || outCap == 0) {
    return;
  }
  out[0] = '\0';
  if (name == nullptr) {
    return;
  }
  size_t n = strlen(name);
  if (n >= 4 && strcasecmp(name + (n - 4), ".txt") == 0) {
    n -= 4;
  }
  if (n >= outCap) {
    n = outCap - 1;
  }
  memcpy(out, name, n);
  out[n] = '\0';
}

static bool readConfigFile(QuoteConfig* cfg) {
  File32 f = sd.open("config.txt", O_RDONLY);
  if (!f) {
    return false;
  }
  char line[64];
  size_t len = 0;
  while (f.available()) {
    const int c = f.read();
    if (c < 0) {
      break;
    }
    if (c == '\n') {
      line[len] = '\0';
      configApplyLine(cfg, line);
      len = 0;
      continue;
    }
    if (c == '\r') {
      continue;
    }
    if (len + 1 < sizeof(line)) {
      line[len++] = static_cast<char>(c);
    }
  }
  if (len > 0) {
    line[len] = '\0';
    configApplyLine(cfg, line);
  }
  f.close();
  return true;
}

static uint16_t countEligibleInFile(File32& f) {
  QuoteLineScanner scanner;
  char buf[64];
  while (true) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) {
      scanner.feed(nullptr, 0, true);
      break;
    }
    scanner.feed(buf, static_cast<size_t>(n), false);
  }
  f.rewind();
  return scanner.eligible;
}

static void readDeckHeader(File32& f, char* out, size_t outCap) {
  QuoteHeaderReader reader;
  reader.begin(out, outCap);
  char buf[64];
  while (!reader.done) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) {
      reader.feed(nullptr, 0, true);
      break;
    }
    reader.feed(buf, static_cast<size_t>(n), false);
  }
  f.rewind();
}

// Red header: the file's first "# title" line, or the filename if it has none.
static void fillDeckLabel(File32& f, const char* name, char* label, size_t labelCap) {
  if (label == nullptr || labelCap == 0) {
    return;
  }
  readDeckHeader(f, label, labelCap);
  if (label[0] == '\0') {
    deckLabelFromName(name, label, labelCap);
  }
}

static bool pickLineInFile(File32& f, uint16_t index, char* out, size_t outCap) {
  QuoteLinePicker picker;
  picker.begin(index, out, outCap);
  char buf[64];
  while (!picker.done) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) {
      picker.feed(nullptr, 0, true);
      break;
    }
    picker.feed(buf, static_cast<size_t>(n), false);
  }
  return picker.done && out[0] != '\0';
}

static bool isTxtFileName(File32& file, char* name, size_t nameCap) {
  if (!file.isFile()) {
    return false;
  }
  if (!file.getName(name, nameCap)) {
    return false;
  }
  return quoteDeckFileName(name);
}

static bool openQuotesDir(File32& dir) {
  return dir.open("quotes", O_RDONLY) && dir.isDirectory();
}

static uint32_t sumDeckBytesAndCount(const char* deck, uint16_t* lineCountOut,
                                     char* firstName, size_t firstNameCap) {
  uint32_t bytes = 0;
  uint16_t lines = 0;
  if (firstName != nullptr && firstNameCap > 0) {
    firstName[0] = '\0';
  }

  if (deck != nullptr && strcmp(deck, "*") != 0) {
    char path[48];
    snprintf(path, sizeof(path), "quotes/%s", deck);
    File32 f = sd.open(path, O_RDONLY);
    if (!f) {
      if (lineCountOut != nullptr) {
        *lineCountOut = 0;
      }
      return 0;
    }
    bytes = f.fileSize();
    lines = countEligibleInFile(f);
    f.close();
    if (firstName != nullptr && firstNameCap > 0) {
      strncpy(firstName, deck, firstNameCap - 1);
      firstName[firstNameCap - 1] = '\0';
    }
    if (lineCountOut != nullptr) {
      *lineCountOut = lines;
    }
    return bytes;
  }

  File32 dir;
  if (!openQuotesDir(dir)) {
    if (lineCountOut != nullptr) {
      *lineCountOut = 0;
    }
    return 0;
  }

  File32 file;
  char name[32];
  while (file.openNext(&dir, O_RDONLY)) {
    if (isTxtFileName(file, name, sizeof(name))) {
      if (firstName != nullptr && firstNameCap > 0 && firstName[0] == '\0') {
        strncpy(firstName, name, firstNameCap - 1);
        firstName[firstNameCap - 1] = '\0';
      }
      bytes += file.fileSize();
      lines = static_cast<uint16_t>(lines + countEligibleInFile(file));
    }
    file.close();
  }
  dir.close();

  if (lineCountOut != nullptr) {
    *lineCountOut = lines;
  }
  return bytes;
}

static bool pickFromAll(uint16_t index, char* out, size_t outCap, char* label,
                        size_t labelCap) {
  File32 dir;
  if (!openQuotesDir(dir)) {
    return false;
  }

  uint16_t remaining = index;
  File32 file;
  char name[32];
  bool ok = false;

  while (file.openNext(&dir, O_RDONLY)) {
    if (!isTxtFileName(file, name, sizeof(name))) {
      file.close();
      continue;
    }
    const uint16_t count = countEligibleInFile(file);
    if (remaining < count) {
      fillDeckLabel(file, name, label, labelCap);
      ok = pickLineInFile(file, remaining, out, outCap);
      file.close();
      break;
    }
    remaining = static_cast<uint16_t>(remaining - count);
    file.close();
  }
  dir.close();
  return ok;
}

SdLoadStatus sdLoadQuote(QuotePayload* out) {
  if (out == nullptr) {
    return SdLoadStatus::NoQuote;
  }
  out->ok = false;
  out->line[0] = '\0';
  out->deckLabel[0] = '\0';
  out->config = QuoteConfig{};

  if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(4))) {
    // Retry at a slower init clock.
    if (!sd.begin(PIN_SD_CS, SD_SCK_MHZ(1))) {
      return SdLoadStatus::NoCard;
    }
  }

  readConfigFile(&out->config);

  // If deck is a missing specific file, fall back to *.
  if (strcmp(out->config.deck, "*") != 0) {
    char path[48];
    snprintf(path, sizeof(path), "quotes/%s", out->config.deck);
    if (!sd.exists(path)) {
      strncpy(out->config.deck, "*", sizeof(out->config.deck) - 1);
      out->config.deck[sizeof(out->config.deck) - 1] = '\0';
    }
  }

  char resolvedName[32] = "";
  uint16_t lineCount = 0;
  const uint32_t bytes =
      sumDeckBytesAndCount(out->config.deck, &lineCount, resolvedName,
                           sizeof(resolvedName));
  if (lineCount == 0) {
    return SdLoadStatus::NoQuote;
  }

  EepromQuoteState st;
  eepromStateLoad(&st);
  const bool deckChanged = strncmp(st.deckKey, out->config.deck,
                                   sizeof(st.deckKey)) != 0;
  const bool sizeChanged = st.fileSize != bytes || st.lineCount != lineCount;
  if (deckChanged || sizeChanged) {
    st.magic = EEPROM_QUOTE_MAGIC;
    st.fileSize = bytes;
    st.lineCount = lineCount;
    QuoteShuffleState shuffle;
    quoteShuffleBegin(&shuffle, lineCount, bytes);
    st.nextIndex = shuffle.cursor;
    st.shuffleStart = shuffle.start;
    st.shuffleStride = shuffle.stride;
    strncpy(st.deckKey, out->config.deck, sizeof(st.deckKey) - 1);
    st.deckKey[sizeof(st.deckKey) - 1] = '\0';
  }

  uint16_t index = 0;
  if (out->config.order == QuoteOrder::Random) {
    // One shuffled pass, remembered across sleep and reset. A bad stride
    // (blank or corrupt EEPROM) starts a new pass inside quoteShuffleTake.
    QuoteShuffleState shuffle;
    shuffle.cursor = st.nextIndex;
    shuffle.start = st.shuffleStart;
    shuffle.stride = st.shuffleStride;
    index = quoteShuffleTake(&shuffle, lineCount);
    st.nextIndex = shuffle.cursor;
    st.shuffleStart = shuffle.start;
    st.shuffleStride = shuffle.stride;
  } else {
    uint16_t next = 0;
    index = quoteNextSequentialIndex(st.nextIndex, lineCount, &next);
    st.nextIndex = next;
  }
  eepromStateSave(&st);

  bool ok = false;
  if (strcmp(out->config.deck, "*") == 0) {
    ok = pickFromAll(index, out->line, sizeof(out->line), out->deckLabel,
                     sizeof(out->deckLabel));
    if (!ok && out->deckLabel[0] == '\0') {
      strncpy(out->deckLabel, "all", sizeof(out->deckLabel) - 1);
    }
  } else {
    char path[48];
    snprintf(path, sizeof(path), "quotes/%s", out->config.deck);
    File32 f = sd.open(path, O_RDONLY);
    if (f) {
      fillDeckLabel(f, out->config.deck, out->deckLabel, sizeof(out->deckLabel));
      ok = pickLineInFile(f, index, out->line, sizeof(out->line));
      f.close();
    } else {
      deckLabelFromName(out->config.deck, out->deckLabel, sizeof(out->deckLabel));
    }
  }

  out->ok = ok;
  return ok ? SdLoadStatus::Ok : SdLoadStatus::NoQuote;
}
