#pragma once

#include <stdint.h>

// Adafruit GFX glyph. Packed so the PROGMEM stride matches the field list.
struct GfxGlyph {
  uint16_t bitmapOffset;
  uint8_t width;
  uint8_t height;
  uint8_t xAdvance;
  int8_t xOffset;
  int8_t yOffset;
} __attribute__((packed));

struct GfxFont {
  const uint8_t* bitmap;
  const GfxGlyph* glyphs;
  uint16_t first;
  uint16_t last;
  uint8_t yAdvance;
};

// FreeSerif 9pt. Body text uses the regular face; the red header uses bold.
extern const GfxFont FONT_SERIF;
extern const GfxFont FONT_SERIF_BOLD;

// Both faces share these metrics. Ascent and descent are ink around the
// baseline; yAdvance is the distance from one baseline to the next.
static constexpr uint8_t FONT_Y_ADVANCE = 22;
static constexpr uint8_t FONT_ASCENT = 12;
static constexpr uint8_t FONT_DESCENT = 5;

uint16_t fontTextWidth(const GfxFont* font, const char* s, uint8_t len);

// True when the pixel is ink. Coordinates are relative to the cursor on
// the baseline (y grows downward, matching the panel).
bool fontTextPixel(const GfxFont* font, const char* s, uint8_t len,
                   int16_t originX, int16_t baseline, int16_t x, int16_t y);
