#pragma once

#include <stdint.h>

// Stop quote refreshes at or below this, matching the other e-ink display.
// Resume only after charging through the band between these two, so a cell
// sitting on the knee does not repaint every wake.
static constexpr int BATTERY_LOW_PERCENT = 5;
static constexpr int BATTERY_RESUME_PERCENT = 15;

// UI estimate for a resting 1S Li-ion cell, not a coulomb count.
// The last step continues to 0% at 3.45 V, so 5% is 3.50 V and 15% is 3.60 V.
struct BatteryCurvePoint {
  uint16_t millivolts;
  uint8_t percent;
};

inline constexpr BatteryCurvePoint kBatteryCurve[] = {
    {3450, 0}, {3550, 10}, {3650, 20}, {3750, 40},
    {3850, 60}, {4000, 80}, {4200, 100},
};

// Cell millivolts → 0..100 from the curve above.
inline int batteryPercentFromMillivolts(uint16_t cellMv) {
  constexpr int n = static_cast<int>(sizeof(kBatteryCurve) / sizeof(kBatteryCurve[0]));
  if (cellMv <= kBatteryCurve[0].millivolts) {
    return 0;
  }
  if (cellMv >= kBatteryCurve[n - 1].millivolts) {
    return 100;
  }
  for (int i = 1; i < n; ++i) {
    if (cellMv > kBatteryCurve[i].millivolts) {
      continue;
    }
    const uint16_t v0 = kBatteryCurve[i - 1].millivolts;
    const uint16_t v1 = kBatteryCurve[i].millivolts;
    const int p0 = kBatteryCurve[i - 1].percent;
    const int p1 = kBatteryCurve[i].percent;
    const uint32_t span = static_cast<uint32_t>(v1 - v0);
    const uint32_t num =
        static_cast<uint32_t>(cellMv - v0) * static_cast<uint32_t>(p1 - p0);
    return p0 + static_cast<int>((num + span / 2) / span);
  }
  return 100;
}

// Returns 0..100, or -1 if the conversion fails.
int batteryReadPercent();
