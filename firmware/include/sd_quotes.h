#pragma once

#include <stdint.h>

#include "config_parse.h"
#include "quote_select.h"

struct QuotePayload {
  QuoteConfig config;
  char line[QUOTE_LINE_MAX];
  char deckLabel[28];  // filename without .txt, or "all"
  bool ok = false;
};

// Power must already be on for the SD rail; SPI bus must be active.
// Returns true if a quote was loaded into out.
bool sdLoadQuote(QuotePayload* out);
