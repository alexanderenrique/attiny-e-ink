#pragma once

// Schematic-derived pin map for ATtiny3226 quote display.
// P-channel MOSFETs (DMG2301L): gate LOW = power ON.
// Names avoid megaTinyCore's PIN_SPI_* macros (default SPI on PA1–PA3).

#include <Arduino.h>

// ---------------------------------------------------------------------------
// SPI0 ALT1 — SCK PC0, MISO PC1, MOSI PC2
// PC3 is ALT1 SS but is wired as the e-ink power gate (GPIO).
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_BUS_SCK  = PIN_PC0;
static constexpr uint8_t PIN_BUS_MISO = PIN_PC1;
static constexpr uint8_t PIN_BUS_MOSI = PIN_PC2;

// ---------------------------------------------------------------------------
// microSD — power gate + CS (active LOW)
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_SD_PWR = PIN_PB1;  // SD_MOSFET
static constexpr uint8_t PIN_SD_CS  = PIN_PB0;  // CS_SD

// ---------------------------------------------------------------------------
// E-paper (GDEM029C90 / SSD1680)
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_EPD_PWR  = PIN_PC3;  // E-Ink_MOSFET
static constexpr uint8_t PIN_EPD_CS   = PIN_PA2;  // CS_E-ink
static constexpr uint8_t PIN_EPD_DC   = PIN_PA1;  // DC
static constexpr uint8_t PIN_EPD_RST  = PIN_PB3;  // RES
static constexpr uint8_t PIN_EPD_BUSY = PIN_PA3;  // BUSY (active HIGH)

// ---------------------------------------------------------------------------
// Battery sense — P-FET gate enables divider onto ADC
// Equal resistors (10 kΩ / 10 kΩ): V_Bat = 2 × V_Sense.
// Full cell 4.20 V → 2.10 V at PA4, under the 2.50 V internal ADC reference.
// (R8/R9 are J4 pulldowns, not the divider.)
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_VSENSE_FET = PIN_PA5;  // V_Sense_MOSFET
static constexpr uint8_t PIN_VSENSE_ADC = PIN_PA4;  // V_Sense (AIN4)
static constexpr uint16_t BATTERY_ADC_REF_MV = 2500;
static constexpr uint16_t BATTERY_ADC_FULL_SCALE = 4096;  // 12-bit, code == Vref

// ---------------------------------------------------------------------------
// User button — active LOW, wakes from power-down
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_BUTTON = PIN_PB2;

// ---------------------------------------------------------------------------
// Unused GPIO — drive low to avoid floating inputs
// ---------------------------------------------------------------------------
static constexpr uint8_t PIN_UNUSED_PA6 = PIN_PA6;
static constexpr uint8_t PIN_UNUSED_PA7 = PIN_PA7;
static constexpr uint8_t PIN_UNUSED_PB4 = PIN_PB4;
static constexpr uint8_t PIN_UNUSED_PB5 = PIN_PB5;
