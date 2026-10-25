#include "display_raster.h"

#include <string.h>

namespace {

struct StampTarget {
  uint8_t* black;
  uint8_t* red;
  uint16_t pageNativeY;
  bool redInk;
};

void stampInk(int16_t x, int16_t y, void* ctx) {
  auto* target = static_cast<StampTarget*>(ctx);
  uint16_t index = 0;
  uint8_t mask = 0;
  if (!rasterPageAddress(target->pageNativeY, x, y, &index, &mask)) {
    return;
  }
  // First ink wins: a later glyph or span must not cover an earlier one.
  const bool occupied = (target->black[index] & mask) == 0 ||
                        (target->red[index] & mask) != 0;
  if (occupied) {
    return;
  }
  if (target->redInk) {
    target->red[index] = static_cast<uint8_t>(target->red[index] | mask);
  } else {
    target->black[index] = static_cast<uint8_t>(target->black[index] & ~mask);
  }
}

}  // namespace

void rasterFillPage(const RasterSpan* spans, uint8_t spanCount,
                    uint16_t pageNativeY, uint8_t* black, uint8_t* red) {
  if (black == nullptr || red == nullptr) {
    return;
  }
  memset(black, 0xFF, EPD_PAGE_BYTES);
  memset(red, 0x00, EPD_PAGE_BYTES);
  if (spans == nullptr || spanCount == 0) {
    return;
  }

  // This band is 8 consecutive landscape columns. Native y 0 is the right edge.
  const int16_t clipX0 = static_cast<int16_t>(EPD_NATIVE_HEIGHT - pageNativeY -
                                              EPD_PAGE_ROWS);
  const int16_t clipX1 =
      static_cast<int16_t>(EPD_NATIVE_HEIGHT - pageNativeY);

  StampTarget target;
  target.black = black;
  target.red = red;
  target.pageNativeY = pageNativeY;
  target.redInk = false;

  for (uint8_t i = 0; i < spanCount; ++i) {
    const RasterSpan& span = spans[i];
    if (span.font == nullptr || span.text == nullptr || span.len == 0) {
      continue;
    }
    if (span.originX >= clipX1) {
      continue;
    }
    const int32_t inkRight = static_cast<int32_t>(span.originX) + span.advance +
                             FONT_RIGHT_OVERHANG;
    if (inkRight <= clipX0) {
      continue;
    }
    target.redInk = span.red;
    fontVisitInk(span.font, span.text, span.len, span.originX, span.baseline,
                 clipX0, clipX1, stampInk, &target);
  }
}
