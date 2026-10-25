#pragma once

// Landscape text → SSD1680 page bytes. No Arduino / SPI types, so host tests
// can check the bytes against the per-pixel font probe.

#include <stdint.h>

#include "display.h"
#include "font.h"

// Eight native rows. Native width is 128, so a page is 128 bytes.
static constexpr uint8_t EPD_PAGE_ROWS = 8;
static constexpr uint16_t EPD_PAGE_BYTES =
    static_cast<uint16_t>(EPD_PAGE_ROWS * (EPD_NATIVE_WIDTH / 8));

static_assert(EPD_NATIVE_HEIGHT % EPD_PAGE_ROWS == 0, "native height / page");
static_assert(EPD_PAGE_BYTES == 128, "expected 8*16 byte pages");

// One run of text. Earlier spans win where ink overlaps (battery, then lines).
struct RasterSpan {
  const GfxFont* font;
  const char* text;
  uint16_t advance;
  int16_t originX;
  int16_t baseline;
  uint8_t len;
  bool red;
};

// Landscape (lx, ly) inside this native-y page → byte index and bit mask.
// Bit 7 of a byte is the leftmost native pixel, matching the panel write.
inline bool rasterPageAddress(uint16_t pageNativeY, int16_t lx, int16_t ly,
                              uint16_t* index, uint8_t* mask) {
  if (index == nullptr || mask == nullptr || lx < 0 || ly < 0 ||
      lx >= static_cast<int16_t>(EPD_WIDTH) ||
      ly >= static_cast<int16_t>(EPD_HEIGHT)) {
    return false;
  }
  const int16_t nativeY =
      static_cast<int16_t>(EPD_NATIVE_HEIGHT - 1 - lx);
  if (nativeY < static_cast<int16_t>(pageNativeY) ||
      nativeY >= static_cast<int16_t>(pageNativeY + EPD_PAGE_ROWS)) {
    return false;
  }
  const uint16_t row = static_cast<uint16_t>(nativeY - pageNativeY);
  const uint16_t xb = static_cast<uint16_t>(ly) >> 3;
  const uint8_t bit = static_cast<uint8_t>(ly & 7);
  *index = static_cast<uint16_t>(row * (EPD_NATIVE_WIDTH / 8) + xb);
  *mask = static_cast<uint8_t>(0x80u >> bit);
  return true;
}

enum class RasterInk : uint8_t { White = 0, Black = 1, Red = 2 };

inline RasterInk rasterPageInk(const uint8_t* black, const uint8_t* red,
                               uint16_t pageNativeY, int16_t lx, int16_t ly) {
  uint16_t index = 0;
  uint8_t mask = 0;
  if (black == nullptr || red == nullptr ||
      !rasterPageAddress(pageNativeY, lx, ly, &index, &mask)) {
    return RasterInk::White;
  }
  if ((red[index] & mask) != 0) {
    return RasterInk::Red;
  }
  if ((black[index] & mask) == 0) {
    return RasterInk::Black;
  }
  return RasterInk::White;
}

// Black page: 1 = white, 0 = black. Red page: 0 = white, 1 = red.
// pageNativeY is the first native row of an 8-row band.
void rasterFillPage(const RasterSpan* spans, uint8_t spanCount,
                    uint16_t pageNativeY, uint8_t* black, uint8_t* red);
