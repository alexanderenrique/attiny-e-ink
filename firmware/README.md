# ATtiny3226 quote display firmware

Ultra-low-power quote / fact display for the `attiny-e-ink` PCB:

- **MCU:** ATtiny3226 (megaTinyCore), 10 MHz, Power-Down idle
- **Panel:** Good Display GDEM029C90 (128×296, black/white/red, SSD1680)
- **Storage:** microSD (FAT32) with plain-text quote decks
- **Power:** P-FET rails for SD and e-paper; PIT + button wake between updates

See [POWER.md](POWER.md) for the full pin/peripheral sleep audit and current-measurement builds.

## Card layout

Format the card **FAT32** (Disk Utility “MS-DOS (FAT)”, ≤32 GB). Copy the contents of [`sdcard/`](sdcard/):

```text
config.txt
quotes/
  stoic.txt
  facts.txt
```

### `config.txt`

```text
interval=7
deck=stoic.txt
order=sequential
```

| Key | Values |
|-----|--------|
| `interval` | Hours between refreshes, 1–18 (approx.; internal OSCULP32K / PIT) |
| `deck` | Filename in `quotes/`, or `*` for every `.txt` |
| `order` | `sequential` or `random` |

### Quote files

One quote per line. Blank lines and `#` comments are skipped. Optional author after ` | `:

```text
The only true wisdom is in knowing you know nothing. | Socrates
We suffer more in imagination than in reality.
```

## Build / flash

```bash
cd firmware
pio run -e quote
pio run -e quote -t upload --upload-port /dev/cu.usbserial-XXXX
```

Burn fuses once after changing BOD / clock options (megaTinyCore / PlatformIO `fuses` target) so `board_hardware.bod = 2.6v` takes effect.

## Host tests

```bash
./native/run_tests.sh
```

## Wake behavior

The fetch-and-display cycle runs when **either** the configured interval elapses **or** PB2 is pressed. Intermediate 32 s PIT ticks only accumulate time.

1. Read battery (10 kΩ / 10 kΩ divider, internal 2.5 V ADC reference, then ADC off)
2. At or below 5% (~3.50 V on the UI curve), paint “Battery Low / Please Charge” once and skip later refreshes until the cell is back to 15% (~3.60 V). Wakes in between only re-read the ADC.
3. Power SD → read config + one quote → power SD off
4. Power panel → draw → SSD1680 deep sleep → cut panel rail
5. `prepareForPowerDown()` then Power-Down until interval or button
