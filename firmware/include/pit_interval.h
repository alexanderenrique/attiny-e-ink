#pragma once

// Pure PIT interval math — compiles on host and on the MCU.

#include <stdint.h>

// OSC1K (OSCULP32K / 32) with RTC_PERIOD_CYC32768 → one PIT interrupt every
// 32768 / 1024 = 32 seconds.
constexpr uint16_t kPitPeriodSeconds = 32;

// How many PIT wakes are needed to cover at least `seconds` of wall time.
// A zero request still needs one wake so the caller cannot spin forever.
inline uint32_t pitWakesForSeconds(uint32_t seconds) {
  if (seconds == 0) {
    return 1;
  }
  return (seconds + static_cast<uint32_t>(kPitPeriodSeconds) - 1U) /
         static_cast<uint32_t>(kPitPeriodSeconds);
}

inline uint32_t pitWakesForHours(uint16_t hours) {
  if (hours == 0) {
    hours = 1;
  }
  return pitWakesForSeconds(static_cast<uint32_t>(hours) * 3600UL);
}
