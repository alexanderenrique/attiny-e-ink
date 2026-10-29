#pragma once

#include <stdint.h>

// EEPROM layout for deck progress. Random order uses nextIndex as the
// shuffle cursor, plus a start and stride for the current pass.
// QDK2 discarded QDK1 records, which had no shuffle fields. Random mode
// never advanced nextIndex, so those records cannot be resumed.
struct EepromQuoteState {
  uint32_t magic;
  uint32_t fileSize;
  uint16_t lineCount;
  uint16_t nextIndex;
  uint16_t shuffleStart;
  uint16_t shuffleStride;
  char deckKey[32];
};

static constexpr uint32_t EEPROM_QUOTE_MAGIC = 0x51444B32UL;  // "QDK2"

void eepromStateLoad(EepromQuoteState* state);
void eepromStateSave(const EepromQuoteState* state);
