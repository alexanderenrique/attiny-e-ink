#include "font.h"

#include <avr/pgmspace.h>
#include <string.h>

static_assert(sizeof(GfxGlyph) == 7, "GfxGlyph must stay packed");

static void loadGlyph(const GfxFont* font, char c, GfxGlyph* out) {
  unsigned char uc = static_cast<unsigned char>(c);
  if (uc < font->first || uc > font->last) {
    uc = '?';
  }
  const uint16_t index = static_cast<uint16_t>(uc - font->first);
  memcpy_P(out, &font->glyphs[index], sizeof(GfxGlyph));
}

static bool glyphPixel(const GfxFont* font, const GfxGlyph* g, int16_t xx,
                       int16_t yy) {
  if (xx < 0 || yy < 0 || xx >= g->width || yy >= g->height || g->width == 0 ||
      g->height == 0) {
    return false;
  }
  const uint16_t bitIndex = static_cast<uint16_t>(
      static_cast<uint16_t>(yy) * g->width + static_cast<uint16_t>(xx));
  const uint8_t bits = pgm_read_byte(
      &font->bitmap[g->bitmapOffset + static_cast<uint16_t>(bitIndex >> 3)]);
  const uint8_t mask = static_cast<uint8_t>(0x80u >> (bitIndex & 7));
  return (bits & mask) != 0;
}

uint16_t fontTextWidth(const GfxFont* font, const char* s, uint8_t len) {
  uint16_t width = 0;
  if (font == nullptr || s == nullptr) {
    return 0;
  }
  for (uint8_t i = 0; i < len; ++i) {
    GfxGlyph g;
    loadGlyph(font, s[i], &g);
    width = static_cast<uint16_t>(width + g.xAdvance);
  }
  return width;
}

bool fontTextPixel(const GfxFont* font, const char* s, uint8_t len,
                   int16_t originX, int16_t baseline, int16_t x, int16_t y) {
  if (font == nullptr || s == nullptr || len == 0) {
    return false;
  }
  int16_t cursor = originX;
  for (uint8_t i = 0; i < len; ++i) {
    GfxGlyph g;
    loadGlyph(font, s[i], &g);
    // xOffset is never negative for these faces, so later glyphs are further right.
    if (static_cast<int16_t>(cursor + g.xOffset) > x) {
      break;
    }
    if (glyphPixel(font, &g, static_cast<int16_t>(x - cursor - g.xOffset),
                   static_cast<int16_t>(y - baseline - g.yOffset))) {
      return true;
    }
    cursor = static_cast<int16_t>(cursor + g.xAdvance);
  }
  return false;
}
