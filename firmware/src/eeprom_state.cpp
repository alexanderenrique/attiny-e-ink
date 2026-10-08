#include "eeprom_state.h"

#include <EEPROM.h>
#include <string.h>

static constexpr int EEPROM_BASE = 0;

void eepromStateLoad(EepromQuoteState* state) {
  if (state == nullptr) {
    return;
  }
  EEPROM.get(EEPROM_BASE, *state);
  if (state->magic != EEPROM_QUOTE_MAGIC) {
    state->magic = EEPROM_QUOTE_MAGIC;
    state->fileSize = 0;
    state->lineCount = 0;
    state->nextIndex = 0;
    state->deckKey[0] = '\0';
  }
}

void eepromStateSave(const EepromQuoteState* state) {
  if (state == nullptr) {
    return;
  }
  EEPROM.put(EEPROM_BASE, *state);
}
