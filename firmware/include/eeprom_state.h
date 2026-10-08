#pragma once

#include <stdint.h>

// EEPROM layout for sequential deck progress.
struct EepromQuoteState {
  uint32_t magic;
  uint32_t fileSize;
  uint16_t lineCount;
  uint16_t nextIndex;
  char deckKey[32];
};

static constexpr uint32_t EEPROM_QUOTE_MAGIC = 0x51444B31UL;  // "QDK1"

void eepromStateLoad(EepromQuoteState* state);
void eepromStateSave(const EepromQuoteState* state);
