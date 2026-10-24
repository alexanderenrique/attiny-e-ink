#pragma once

#include <stdint.h>

// Stop quote refreshes at or below this, matching the other e-ink display.
// Resume only after charging through the band between these two, so a cell
// sitting on the knee does not repaint every wake.
static constexpr int BATTERY_LOW_PERCENT = 5;
static constexpr int BATTERY_RESUME_PERCENT = 15;

// Healthy cells are sampled once per this many update cycles. The divider
// is off on the wakes in between; the last percent is reused. A missing
// reading, the low-battery warning, or a cell still at or below the resume
// threshold samples every wake so the cliff and a charge are not missed.
static constexpr uint8_t BATTERY_SAMPLE_EVERY = 5;

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

// wakesSinceSample is 0 on the wake after a successful measurement.
// True when this update should enable the divider.
inline bool batteryShouldSample(uint8_t wakesSinceSample, int lastPercent,
                                bool batteryLow) {
  if (lastPercent < 0 || batteryLow || lastPercent <= BATTERY_RESUME_PERCENT) {
    return true;
  }
  return wakesSinceSample >= static_cast<uint8_t>(BATTERY_SAMPLE_EVERY - 1);
}

// Schedules a divider read. Advances the gap when this wake should reuse
// the last percent. After a successful conversion, call batteryCommitSample()
// so the next healthy gaps start over. A failed conversion leaves the counter
// alone and the following wake tries again.
inline bool batteryScheduleSample(uint8_t* wakesSinceSample, int lastPercent,
                                  bool batteryLow) {
  if (batteryShouldSample(*wakesSinceSample, lastPercent, batteryLow)) {
    return true;
  }
  if (*wakesSinceSample < 255) {
    ++(*wakesSinceSample);
  }
  return false;
}

inline void batteryCommitSample(uint8_t* wakesSinceSample) {
  *wakesSinceSample = 0;
}
