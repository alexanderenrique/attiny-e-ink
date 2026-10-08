#pragma once

#include <stdint.h>

// Power-Down until the configured interval elapses or PB2 is pressed.
// Intermediate OSC1K PIT ticks (32 s) only accumulate time; they do not run
// the quote cycle. Timing is approximate (internal OSCULP32K, no crystal).
void sleepUntilNextWake(uint16_t intervalHours);

// Power-Down until PIN_EPD_BUSY falls or a PIT timeout expires.
// Panel rail and control pins must already be powered/driven.
// Returns true if BUSY is low on exit, false on timeout.
bool sleepUntilBusyLow(uint16_t timeoutSeconds);
