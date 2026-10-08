#include "battery.h"

#include <Arduino.h>

#include "pins.h"
#include "power.h"

int batteryReadPercent() {
  senseDividerOn();

  // adcPowerOff() clears ENABLE between wakes; megaTinyCore only turns the
  // ADC on in init(). 2-series analogRead() does not re-enable it.
  ADC0.CTRLA = ADC_ENABLE_bm | ADC_LOWLAT_bm;
  analogReference(INTERNAL2V5);
  analogReadResolution(12);

  // Internal 2.5 V reference startup, then drop the first conversion.
  delay(2);
  (void)analogRead(PIN_VSENSE_ADC);

  constexpr uint8_t kSamples = 8;
  uint32_t sum = 0;
  for (uint8_t i = 0; i < kSamples; ++i) {
    const int16_t sample = analogRead(PIN_VSENSE_ADC);
    if (sample < 0) {
      senseDividerOff();
      adcPowerOff();
      return -1;
    }
    sum += static_cast<uint32_t>(sample);
  }
  senseDividerOff();
  adcPowerOff();

  // 12-bit code 4096 == Vref. Divider is 2:1, so
  // cell_mV = avg * Vref_mV * 2 / 4096.
  const uint32_t denom =
      static_cast<uint32_t>(kSamples) * BATTERY_ADC_FULL_SCALE;
  const uint32_t cellMv = (sum * (BATTERY_ADC_REF_MV * 2UL) + denom / 2) / denom;
  if (cellMv > 65535UL) {
    return batteryPercentFromMillivolts(65535);
  }
  return batteryPercentFromMillivolts(static_cast<uint16_t>(cellMv));
}
