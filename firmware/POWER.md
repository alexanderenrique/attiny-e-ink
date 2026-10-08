# Power audit — ATtiny3226 quote display

Target idle state: **Power-Down** with OSCULP32K → PIT (32 s ticks) and PB2 falling-edge wake. Every switched rail is off. The quote cycle runs only when the configured interval elapses or the button is pressed.

Reference: ATtiny3224/3226/3227 datasheet (sleep / RTC / BOD), SSD1680 datasheet §9.1.

## Subsystem table

| Subsystem | Active state | Sleep state | Clock source | Wake capability | Expected power concern |
|-----------|--------------|-------------|--------------|-----------------|------------------------|
| CPU | Running @ 10 MHz | Stopped (Power-Down) | Main OSC20M / 2 | — | Race-to-sleep; not left on |
| CLK_PER / main osc | On | Stopped | Internal HF | — | Must be off in Power-Down |
| OSCULP32K + PIT | Armed for interval | Running, PERIOD=CYC32768 on OSC1K | OSCULP32K/32 | PIT interrupt | Only async timer for long idle |
| RTC counter | Off | Off | — | Not used in Power-Down | Must stay disabled |
| TCB1 (millis) | On, RUNSTDBY clear | Frozen (no CLK_PER) | CLK_PER | — | No sleep current when PD |
| TCA0 / TCB0 | Off | Off | — | — | Explicitly disabled |
| ADC0 | On only during sense | Off | CLK_PER | — | Disabled after battery read |
| AC0 / CCL / EVSYS | Off | Off | — | — | Explicitly cleared |
| SPI0 | On during SD/EPD xfer | Off, pins driven low | CLK_PER | — | Back-power if pins high into unpowered chips |
| USART0 / TWI0 | Off | Off | — | — | Explicitly disabled |
| WDT | Off | Off | — | — | Deliberately disabled; no reliability need for sleep current |
| BOD | Enabled active | Sampled 125 Hz | Internal | Reset | Tradeoff: EEPROM + brown-out vs continuous BOD current |
| PB2 button | Input pull-up | Input pull-up + falling IRQ | — | Power-Down wake | Current only while pressed |
| Q1 e-ink P-FET | Gate low (ON) | Gate high (OFF) | — | — | External R1=1M to 3.3V holds OFF at reset |
| Q2 SD P-FET | Gate low (ON) | Gate high (OFF) | — | — | External R2=1M to 3.3V holds OFF at reset |
| Q3 sense P-FET | Gate low (ON) | High-Z (R5 to V_Bat) | — | — | **Hardware residual:** PA5 ESD diode clamps gate ≈VDD+0.3 when V_Bat≳3.6 V |
| SSD1680 / panel | SPI + update | Deep sleep then VCI cut | Internal booster while powered | BUSY falling (during update only) | Image retained with power off |
| microSD | SPI | Rail off, CS/bus low | — | — | Shared SPI can back-power during opposite rail ON |
| Battery divider | Q3 on briefly; ADC ref internal 2.5 V | Off (R3 holds PA4 at GND) | — | — | Midpoint is analog (~1.7–2.1 V) → PA4 input buffer disabled |
| TPS7A0533 LDO | Always on | Always on | — | — | Always-on 3.3 V domain; not firmware-switched |
| MCP73832 + STAT LED | USB charge only | Off when unplugged | — | — | Not in battery idle budget |

## GPIO table

| Pin | Connected device | Active configuration | Sleep configuration | Pull-up/down | Digital input enabled? | Back-power risk? |
|-----|------------------|----------------------|---------------------|--------------|------------------------|------------------|
| PA0 | UPDI | UPDI (core) | Untouched | External programmer | N/A | No |
| PA1 | EPD DC | Out, driven per SPI cmd | Out low | None | N/A (out) | Yes if high into unpowered EPD |
| PA2 | EPD CS | Out, idle high when rail on | Out low | None | N/A | Yes if high into unpowered EPD |
| PA3 | EPD BUSY | In, falling IRQ during wait | In, buffer **disabled** | None | No (sleep) | Pull-up would source into unpowered SSD1680 |
| PA4 | V_Sense (ADC) | Analog, buffer disabled | Buffer disabled | R3 to GND | No | No (grounded via R3 when Q3 off) |
| PA5 | V_Sense MOSFET gate | Out low = ON | High-Z, buffer disabled | R5=1M to **V_Bat** | No | ESD clamp vs V_Bat (see above) |
| PA6 | Unused | Out low | Out low | None | N/A | No |
| PA7 | Unused | Out low | Out low | None | N/A | No |
| PB0 | SD CS | Out, idle high when rail on | Out low | None | N/A | Yes if high into unpowered SD |
| PB1 | SD MOSFET gate | Out low = ON | Out high = OFF | R2=1M to 3.3V | N/A | No |
| PB2 | Button | In pull-up | In pull-up + falling IRQ | Internal PU | Yes | No |
| PB3 | EPD RES | Out high when rail on | Out low | None | N/A | Yes if high into unpowered EPD |
| PB4 | Unused | Out low | Out low | None | N/A | No |
| PB5 | Unused | Out low | Out low | None | N/A | No |
| PC0 | SPI SCK (shared) | SPI / out | Out low | None | N/A | Shared bus: highs reach both loads |
| PC1 | SPI MISO (shared) | SPI | **Out low** (not pull-up) | None | N/A | Pull-up would back-power SD |
| PC2 | SPI MOSI (shared) | SPI / out | Out low | None | N/A | Shared bus |
| PC3 | E-Ink MOSFET gate | Out low = ON | Out high = OFF | R1=1M to 3.3V | N/A | No |

## Flags / open items

1. **Q3 gate ESD clamp** — When `V_Bat` is above ~3.6 V, PA5’s protection diode holds the gate near VDD, so Q3 may leak. Firmware uses high-Z + R5; a board spin needs an open-drain / N-FET level shift for a true OFF.
2. **Shared SPI without series resistors** — While one rail is on, the other load’s inputs still see bus activity. Parking low between domains is necessary and not sufficient. Series resistors or separate buses would close this.
3. **BOD fuse level** — `board_hardware.bod = 2.6v` takes effect after a fuse burn / UPDI fuse write. Runtime `BOD.CTRLA` sets sampled sleep mode regardless.
4. **Watchdog** — Left disabled. Enabling it would add Power-Down current for a reliability benefit this product does not require (button still wakes).

## Diagnostic builds

```bash
pio run -e quote_diag0   # MCU Power-Down, PIT off, button wake
pio run -e quote_diag1   # MCU + 32 s PIT
pio run -e quote_diag2   # EPD rail on, controller deep sleep, MCU PD
pio run -e quote_diag3   # Repeated display update (BUSY sleep)
pio run -e quote_diag4   # One update then long Power-Down + PIT
```

Production `quote` env: no UART, no status LED toggling.

## Always-on during long idle (and why)

- OSCULP32K + PIT — periodic wake timebase  
- PB2 pull-up + async sense — button wake  
- BOD sampled 125 Hz — brown-out before EEPROM corruption  
- TPS7A0533 — system 3.3 V (hardware)  
- Q3 residual leakage when cell is high — hardware limit above  
