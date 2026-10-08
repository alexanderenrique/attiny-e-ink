#include <Arduino.h>

#include "battery.h"
#include "display.h"
#include "power.h"
#include "quote_select.h"
#include "sd_quotes.h"
#include "sleep_rtc.h"

enum class SystemState : uint8_t {
  Boot = 0,
  Active,
  EpdPoweringUp,
  EpdUpdate,
  EpdPoweringDown,
  SleepPrepare,
  PowerDown,
};

static uint16_t g_intervalHours = 7;
static SystemState g_state = SystemState::Boot;
// Survives Power-Down (SRAM retained). Set once the warning is on the glass.
static bool g_batteryLow = false;

static void panelPowerUp() {
  g_state = SystemState::EpdPoweringUp;
  epdPowerOn();
  spiBeginBus();
  g_state = SystemState::EpdUpdate;
}

static void panelPowerDown() {
  g_state = SystemState::EpdPoweringDown;
  spiEndBus();
  epdPowerOff();
}

static void runUpdateCycle() {
  g_state = SystemState::Active;

  const int batteryPct = batteryReadPercent();

  if (batteryPct >= BATTERY_RESUME_PERCENT) {
    g_batteryLow = false;
  } else if (g_batteryLow ||
             (batteryPct >= 0 && batteryPct <= BATTERY_LOW_PERCENT)) {
    // One warning refresh, then leave the glass alone until the cell is
    // back above the resume threshold. Later wakes only re-read the ADC.
    if (!g_batteryLow) {
      panelPowerUp();
      displayShowLowBattery(batteryPct);
      panelPowerDown();
      g_batteryLow = true;
    }
    g_state = SystemState::SleepPrepare;
    return;
  }

  QuotePayload payload;
  payload.ok = false;

  sdPowerOn();
  spiBeginBus();
  const bool loaded = sdLoadQuote(&payload);
  spiEndBus();
  sdPowerOff();

  if (!loaded || !payload.ok) {
    // Leave the previous image on the glass; prepare for sleep.
    g_state = SystemState::SleepPrepare;
    return;
  }

  g_intervalHours = payload.config.intervalHours;
  if (g_intervalHours == 0) {
    g_intervalHours = 1;
  }

  char* quote = payload.line;
  char* author = nullptr;
  quoteSplitAuthor(payload.line, &quote, &author);

  DisplayQuote dq;
  dq.deckLabel = payload.deckLabel;
  dq.quote = quote;
  dq.author = author;
  dq.batteryPercent = batteryPct;

  panelPowerUp();
  displayShowQuote(dq);
  panelPowerDown();
  g_state = SystemState::SleepPrepare;
}

#if defined(LOW_POWER_DIAGNOSTICS) && LOW_POWER_DIAGNOSTICS

#include <SPI.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#include "pins.h"

#ifndef LOW_POWER_DIAG_STATE
#define LOW_POWER_DIAG_STATE 0
#endif

static void diagPowerDownForever(bool enableButton) {
  prepareForPowerDown();
  if (enableButton) {
    PORTB.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_FALLING_gc;
    PORTB.INTFLAGS = PIN2_bm;
  }
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  for (;;) {
    cli();
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
  }
}

static void runDiagnostics() {
  powerBegin();

#if LOW_POWER_DIAG_STATE == 0
  // MCU only in Power-Down (button wake; PIT off).
  diagPowerDownForever(true);
#elif LOW_POWER_DIAG_STATE == 1
  // MCU + PIT running (32 s ticks; long interval, no quote cycle).
  for (;;) {
    sleepUntilNextWake(18);
  }
#elif LOW_POWER_DIAG_STATE == 2
  // Display powered, controller deep-slept, MCU in Power-Down (rail stays on).
  epdPowerOn();
  spiBeginBus();
  // HW reset + deep sleep without cutting Q1.
  digitalWrite(PIN_EPD_RST, LOW);
  delay(2);
  digitalWrite(PIN_EPD_RST, HIGH);
  delay(10);
  {
    // Command 0x10 / 0x01 via the same SPI path as display.cpp would use.
    digitalWrite(PIN_EPD_DC, LOW);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(0x10);
    SPI.endTransaction();
    digitalWrite(PIN_EPD_CS, HIGH);
    digitalWrite(PIN_EPD_DC, HIGH);
    digitalWrite(PIN_EPD_CS, LOW);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));
    SPI.transfer(0x01);
    SPI.endTransaction();
    digitalWrite(PIN_EPD_CS, HIGH);
  }
  delay(100);
  spiEndBus();
  // Keep Q1 on; park SPI; do not call epdCutPowerAfterDeepSleep.
  digitalWrite(PIN_EPD_CS, HIGH);
  digitalWrite(PIN_EPD_DC, HIGH);
  digitalWrite(PIN_EPD_RST, HIGH);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  for (;;) {
    cli();
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
  }
#elif LOW_POWER_DIAG_STATE == 3
  // Repeated full refresh (BUSY Power-Down wait).
  for (;;) {
    runUpdateCycle();
  }
#elif LOW_POWER_DIAG_STATE == 4
  // One update, deep sleep, rail off, then Power-Down with PIT.
  runUpdateCycle();
  for (;;) {
    sleepUntilNextWake(18);
  }
#else
#error "LOW_POWER_DIAG_STATE must be 0..4"
#endif
}

#endif  // LOW_POWER_DIAGNOSTICS

void setup() {
#if defined(LOW_POWER_DIAGNOSTICS) && LOW_POWER_DIAGNOSTICS
  runDiagnostics();
  return;
#endif

  g_state = SystemState::Boot;
  powerBegin();
  runUpdateCycle();
}

void loop() {
#if defined(LOW_POWER_DIAGNOSTICS) && LOW_POWER_DIAGNOSTICS
  for (;;) {
  }
#else
  g_state = SystemState::PowerDown;
  sleepUntilNextWake(g_intervalHours);
  runUpdateCycle();
#endif
}
