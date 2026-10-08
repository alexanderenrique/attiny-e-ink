#pragma once

#include <stdint.h>

static constexpr uint8_t FONT_WIDTH = 8;
static constexpr uint8_t FONT_HEIGHT = 8;
static constexpr uint8_t FONT_FIRST = 0x20;
static constexpr uint8_t FONT_LAST = 0x7E;

// Returns pointer to 8 flash bytes for glyph, or nullptr for missing.
const uint8_t* fontGlyph(char c);

// Advance width (same as FONT_WIDTH for this monospace face).
inline uint8_t fontAdvance(char /*c*/) { return FONT_WIDTH; }
