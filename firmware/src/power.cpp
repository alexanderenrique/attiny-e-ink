#include "power.h"

#include <Arduino.h>
#include <SPI.h>
#include <avr/cpufunc.h>

#include "pins.h"

// Before cutting a peripheral rail, drive shared bus pins low so the
// unpowered chip is not back-powered through ESD diodes.

static void holdSpiPinsLow() {
  digitalWrite(PIN_BUS_SCK, LOW);
  digitalWrite(PIN_BUS_MOSI, LOW);
  digitalWrite(PIN_BUS_MISO, LOW);
  pinMode(PIN_BUS_SCK, OUTPUT);
  pinMode(PIN_BUS_MOSI, OUTPUT);
  pinMode(PIN_BUS_MISO, OUTPUT);
}

static void applySleepGpio() {
  // P-FET gates: high = OFF for Q1/Q2 (source = 3.3 V, external 1 M pull-up).
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, HIGH);

  // Q3 source is V_Bat. Drive high-Z so R5 can pull the gate toward the cell.
  // Do not drive PA5 high: at a full cell that is only ~0.9 V Vgs (near Vth).
  pinMode(PIN_VSENSE_FET, INPUT);
  PORTA.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;

  // Control / SPI lines into unpowered loads: driven low.
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, LOW);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, LOW);
  pinMode(PIN_EPD_DC, OUTPUT);
  digitalWrite(PIN_EPD_DC, LOW);
  pinMode(PIN_EPD_RST, OUTPUT);
  digitalWrite(PIN_EPD_RST, LOW);
  holdSpiPinsLow();

  // BUSY floats into an unpowered SSD1680; disable the input buffer.
  pinMode(PIN_EPD_BUSY, INPUT);
  PORTA.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc;

  // Divider bottom is grounded via R3 when Q3 is off; still disable the buffer.
  pinMode(PIN_VSENSE_ADC, INPUT);
  PORTA.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc;

  // Button wake source: pull-up, interrupt configured by sleep_rtc.
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  PORTB.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_INTDISABLE_gc;

  pinMode(PIN_UNUSED_PA6, OUTPUT);
  digitalWrite(PIN_UNUSED_PA6, LOW);
  pinMode(PIN_UNUSED_PA7, OUTPUT);
  digitalWrite(PIN_UNUSED_PA7, LOW);
  pinMode(PIN_UNUSED_PB4, OUTPUT);
  digitalWrite(PIN_UNUSED_PB4, LOW);
  pinMode(PIN_UNUSED_PB5, OUTPUT);
  digitalWrite(PIN_UNUSED_PB5, LOW);

  // PA0 is UPDI — leave alone.
}

static void disableUnusedPeripherals() {
  // SPI / USART / TWI
  SPI0.CTRLA = 0;
  USART0.CTRLA = 0;
  USART0.CTRLB = 0;
  TWI0.MCTRLA = 0;
  TWI0.SCTRLA = 0;

  // Timers: TCA0 and TCB0 fully off. TCB1 is millis — clear RUNSTDBY only.
  TCA0.SINGLE.CTRLA = 0;
  TCB0.CTRLA = 0;
  TCB1.CTRLA = static_cast<uint8_t>(TCB1.CTRLA & ~TCB_RUNSTDBY_bm);

  // Analog / logic
  ADC0.CTRLA = 0;
  AC0.CTRLA = 0;
  CCL.CTRLA = 0;

  // Event system — clear channel muxes (device has 6 channels on 3226).
  EVSYS.CHANNEL0 = 0;
  EVSYS.CHANNEL1 = 0;
  EVSYS.CHANNEL2 = 0;
  EVSYS.CHANNEL3 = 0;
  EVSYS.CHANNEL4 = 0;
  EVSYS.CHANNEL5 = 0;

  // RTC counter off; PIT is armed separately for wake.
  while (RTC.STATUS & RTC_CTRLABUSY_bm) {
  }
  RTC.CTRLA = 0;
  RTC.INTCTRL = 0;

  // Watchdog oscillator runs in Power-Down if enabled — keep it off.
  _PROTECTED_WRITE(WDT.CTRLA, 0);

  // Active enabled, sleep sampled @ 125 Hz (EEPROM safety vs continuous BOD).
  _PROTECTED_WRITE(BOD.CTRLA, BOD_ACTIVE_ENABLED_gc | BOD_SLEEP_SAMPLED_gc |
                                  BOD_SAMPFREQ_125HZ_gc);
}

void powerBegin() {
  // Stop anything that might claim pins after megaTinyCore boot.
  SPI0.CTRLA = 0;
  USART0.CTRLA = 0;
  USART0.CTRLB = 0;
  TCB1.CTRLA = static_cast<uint8_t>(TCB1.CTRLA & ~TCB_RUNSTDBY_bm);

  // Power gates: Q1/Q2 driven high = OFF. Q3 high-Z (R5 to V_Bat).
  pinMode(PIN_SD_PWR, OUTPUT);
  digitalWrite(PIN_SD_PWR, HIGH);
  pinMode(PIN_EPD_PWR, OUTPUT);
  digitalWrite(PIN_EPD_PWR, HIGH);
  pinMode(PIN_VSENSE_FET, INPUT);
  PORTA.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, LOW);
  pinMode(PIN_EPD_CS, OUTPUT);
  digitalWrite(PIN_EPD_CS, LOW);
  pinMode(PIN_EPD_DC, OUTPUT);
  digitalWrite(PIN_EPD_DC, LOW);
  pinMode(PIN_EPD_RST, OUTPUT);
  digitalWrite(PIN_EPD_RST, LOW);

  pinMode(PIN_EPD_BUSY, INPUT);
  PORTA.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc;

  pinMode(PIN_VSENSE_ADC, INPUT);
  PORTA.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc;

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  pinMode(PIN_UNUSED_PA6, OUTPUT);
  digitalWrite(PIN_UNUSED_PA6, LOW);
  pinMode(PIN_UNUSED_PA7, OUTPUT);
  digitalWrite(PIN_UNUSED_PA7, LOW);
  pinMode(PIN_UNUSED_PB4, OUTPUT);
  digitalWrite(PIN_UNUSED_PB4, LOW);
  pinMode(PIN_UNUSED_PB5, OUTPUT);
  digitalWrite(PIN_UNUSED_PB5, LOW);

  holdSpiPinsLow();
}

void prepareForPowerDown() {
  sdPowerOff();
  epdCutPowerAfterDeepSleep();
  senseDividerOff();
  spiEndBus();
  applySleepGpio();
  disableUnusedPeripherals();
}

void spiBeginBus() {
  // Route SPI0 to PC[2:0] (ALT1). PC3 stays a GPIO for the e-ink FET;
  // megaTinyCore SPI.swap(1) selects that mux without claiming PC3 as SS.
  SPI.swap(1);
  SPI.begin();
}

void spiEndBus() {
  SPI.end();
  holdSpiPinsLow();
}

void sdPowerOn() {
  digitalWrite(PIN_SD_CS, HIGH);
  digitalWrite(PIN_SD_PWR, LOW);
  delay(10);
}

void sdPowerOff() {
  digitalWrite(PIN_SD_CS, LOW);
  holdSpiPinsLow();
  digitalWrite(PIN_SD_PWR, HIGH);
}

void epdPowerOn() {
  // Re-enable BUSY digital input while the panel is powered.
  PORTA.PIN3CTRL = PORT_ISC_INTDISABLE_gc;
  pinMode(PIN_EPD_BUSY, INPUT);

  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);
  digitalWrite(PIN_EPD_PWR, LOW);
  delay(20);
}

void epdPowerOff() {
  epdCutPowerAfterDeepSleep();
}

void epdCutPowerAfterDeepSleep() {
  digitalWrite(PIN_EPD_CS, LOW);
  digitalWrite(PIN_EPD_DC, LOW);
  digitalWrite(PIN_EPD_RST, LOW);
  holdSpiPinsLow();
  digitalWrite(PIN_EPD_PWR, HIGH);
  pinMode(PIN_EPD_BUSY, INPUT);
  PORTA.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc;
}

void senseDividerOn() {
  // Drive gate low to turn Q3 on. Re-enable output first.
  PORTA.PIN5CTRL = 0;
  pinMode(PIN_VSENSE_FET, OUTPUT);
  digitalWrite(PIN_VSENSE_FET, LOW);

  // Analog midpoint (about 1.7–2.1 V). Keep the digital buffer off on PA4.
  pinMode(PIN_VSENSE_ADC, INPUT);
  PORTA.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc;
  delay(2);
}

void senseDividerOff() {
  pinMode(PIN_VSENSE_FET, INPUT);
  PORTA.PIN5CTRL = PORT_ISC_INPUT_DISABLE_gc;
  pinMode(PIN_VSENSE_ADC, INPUT);
  PORTA.PIN4CTRL = PORT_ISC_INPUT_DISABLE_gc;
}

void adcPowerOff() {
  ADC0.CTRLA = 0;
}
