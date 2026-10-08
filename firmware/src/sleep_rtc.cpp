#include "sleep_rtc.h"

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>

#include "pins.h"
#include "pit_interval.h"
#include "power.h"

enum class WakeReason : uint8_t {
  None = 0,
  Pit = 1,
  Button = 2,
  Busy = 3,
};

static volatile WakeReason g_wake = WakeReason::None;

ISR(RTC_PIT_vect) {
  RTC.PITINTFLAGS = RTC_PI_bm;
  g_wake = WakeReason::Pit;
}

ISR(PORTB_PORT_vect) {
  PORTB.INTFLAGS = PIN2_bm;
  g_wake = WakeReason::Button;
}

ISR(PORTA_PORT_vect) {
  PORTA.INTFLAGS = PIN3_bm;
  g_wake = WakeReason::Busy;
}

static void pitStop() {
  while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) {
  }
  RTC.PITCTRLA = 0;
  RTC.PITINTCTRL = 0;
  RTC.PITINTFLAGS = RTC_PI_bm;
}

static void pitArmOnePeriod() {
  // Ultra-low-power internal oscillator / 32 → 1024 Hz.
  _PROTECTED_WRITE(CLKCTRL.OSC32KCTRLA, CLKCTRL_ENABLE_bm);
  RTC.CLKSEL = RTC_CLKSEL_OSC1K_gc;

  // RTC counter stays off in Power-Down; only PIT runs.
  while (RTC.STATUS & RTC_CTRLABUSY_bm) {
  }
  RTC.CTRLA = 0;
  RTC.INTCTRL = 0;
  RTC.INTFLAGS = RTC_OVF_bm | RTC_CMP_bm;

  pitStop();

  while (RTC.PITSTATUS & RTC_CTRLBUSY_bm) {
  }
  RTC.PITINTCTRL = RTC_PI_bm;
  RTC.PITCTRLA = RTC_PITEN_bm | RTC_PERIOD_CYC32768_gc;
}

static void buttonWakeEnable() {
  PORTB.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_FALLING_gc;
  PORTB.INTFLAGS = PIN2_bm;
}

static void buttonWakeDisable() {
  PORTB.PIN2CTRL = PORT_PULLUPEN_bm | PORT_ISC_INTDISABLE_gc;
}

static void busyWakeEnable() {
  // No pull-up: a pull would back-power the panel when the rail is off, and
  // during an update the SSD1680 drives BUSY actively.
  PORTA.PIN3CTRL = PORT_ISC_FALLING_gc;
  PORTA.INTFLAGS = PIN3_bm;
}

static void busyWakeDisable() {
  // Leave input enabled only while the panel rail is on; caller parks it.
  PORTA.PIN3CTRL = PORT_ISC_INTDISABLE_gc;
}

static void powerDownUntilWake() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);

  cli();
  while (g_wake == WakeReason::None) {
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();
    cli();
  }
  sei();
}

static void debounceButtonRelease() {
  delay(20);
  while (digitalRead(PIN_BUTTON) == LOW) {
    delay(10);
  }
  delay(20);
}

void sleepUntilNextWake(uint16_t intervalHours) {
  if (intervalHours == 0) {
    intervalHours = 1;
  }

  const uint32_t wakesNeeded = pitWakesForHours(intervalHours);
  uint32_t wakesSeen = 0;

  prepareForPowerDown();

  buttonWakeEnable();
  pitArmOnePeriod();

  // Refuse to sleep with no wake source.
  if ((PORTB.PIN2CTRL & PORT_ISC_gm) == PORT_ISC_INTDISABLE_gc &&
      (RTC.PITINTCTRL & RTC_PI_bm) == 0) {
    return;
  }

  for (;;) {
    g_wake = WakeReason::None;
    powerDownUntilWake();

    if (g_wake == WakeReason::Button) {
      break;
    }

    // PIT tick: accumulate and sleep again until the interval is done.
    ++wakesSeen;
    if (wakesSeen >= wakesNeeded) {
      break;
    }
  }

  pitStop();
  buttonWakeDisable();

  if (g_wake == WakeReason::Button || digitalRead(PIN_BUTTON) == LOW) {
    debounceButtonRelease();
  }
}

bool sleepUntilBusyLow(uint16_t timeoutSeconds) {
  if (digitalRead(PIN_EPD_BUSY) == LOW) {
    return true;
  }

  const uint32_t wakesNeeded = pitWakesForSeconds(timeoutSeconds);
  uint32_t wakesSeen = 0;

  busyWakeEnable();
  pitArmOnePeriod();

  for (;;) {
    if (digitalRead(PIN_EPD_BUSY) == LOW) {
      pitStop();
      busyWakeDisable();
      return true;
    }

    g_wake = WakeReason::None;
    powerDownUntilWake();

    if (g_wake == WakeReason::Busy || digitalRead(PIN_EPD_BUSY) == LOW) {
      pitStop();
      busyWakeDisable();
      return digitalRead(PIN_EPD_BUSY) == LOW;
    }

    ++wakesSeen;
    if (wakesSeen >= wakesNeeded) {
      break;
    }
  }

  pitStop();
  busyWakeDisable();
  return digitalRead(PIN_EPD_BUSY) == LOW;
}
