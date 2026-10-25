#pragma once

// Host stand-in so font bitmaps compiled with PROGMEM can be read in tests.

#include <stdint.h>
#include <string.h>

#define PROGMEM
#define PGM_P const char*
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define memcpy_P(dest, src, n) memcpy((dest), (src), (n))
