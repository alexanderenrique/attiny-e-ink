#pragma once

#include <stdint.h>

#include "config_parse.h"
#include "quote_select.h"

struct QuotePayload {
  QuoteConfig config;
  char line[QUOTE_LINE_MAX];
  char deckLabel[80];  // first "# title" line, else filename without .txt
  bool ok = false;
};

enum class SdLoadStatus : uint8_t {
  Ok = 0,   // a quote was loaded into out
  NoCard,   // sd.begin failed: missing card or no SPI response
  NoQuote,  // card mounted, but no line could be loaded
};

// Power must already be on for the SD rail; SPI bus must be active.
SdLoadStatus sdLoadQuote(QuotePayload* out);
