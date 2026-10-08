#pragma once

#include <stdint.h>

void powerBegin();

// Audit point before long Power-Down: every rail off, GPIO sleep table,
// unused peripherals disabled, sampled sleep BOD, WDT off.
void prepareForPowerDown();

void spiBeginBus();
void spiEndBus();

void sdPowerOn();
void sdPowerOff();

void epdPowerOn();
void epdPowerOff();

// After SSD1680 deep-sleep command: park pins and cut Q1 without waiting BUSY.
void epdCutPowerAfterDeepSleep();

void senseDividerOn();
void senseDividerOff();

// Disable ADC0 after a battery (or other) conversion burst.
void adcPowerOff();
