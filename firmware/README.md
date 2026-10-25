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

One quote per line. Blank lines are skipped. An optional author follows ` | ` on the same line.

The red header is the first non-blank line when that line starts with `#`:

| File line | Red header |
|-----------|------------|
| `#Fun Facts` | `Fun Facts` |
| `# Fun Facts` | `Fun Facts` |
| `#Cat Facts!` | `Cat Facts!` |
| *(no `#` title)* | filename without `.txt` |

- Leading spaces before `#`, and spaces right after it, are removed. Trailing spaces are removed.
- A `#` with no text after it is not a title, so the filename is used.
- Only that first title line is the header. Later `#` lines are comments and are skipped. The title line itself is never shown as a quote.
- With `deck=*`, each quote uses the title from the file it came from.

The title is the red line. The quote is black FreeSerif, and wraps to at most four lines. Keep each quote to about **140 characters** (roughly **25 words**). Anything past the fourth line is cut off, and one very long word can take a whole line. An author after ` | ` is drawn in black under the quote, so it uses one of those four lines.

```text
#Stoic Quotes
# One quote per line. Optional author after " | "
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

1. Read the battery, or reuse the last percent (see below)
2. At or below 5% (~3.50 V on the UI curve), paint “Battery Low / Please Charge” once and skip later refreshes until the cell is back to 15% (~3.60 V). Wakes in between only re-read the ADC.
3. Power SD → read config + one quote → power SD off. If the card does not answer, paint “No SD card detected” once and leave that image up until a quote loads. Later wakes still try the card.
4. Power panel → draw → SSD1680 deep sleep → cut panel rail
5. `prepareForPowerDown()` then Power-Down until interval or button

The glass stays still until both SSD1680 planes are loaded. Each 8-row band is one slice of the landscape image: the line is walked once and only the glyphs in that slice are decoded into the black and red bytes. Probing every pixel from the start of the string took several seconds on the 10 MHz ATtiny before the waveform could start. The full red/black refresh itself is unchanged.

### Battery checks

The sense divider is 100 kΩ / 100 kΩ, read against the internal 2.5 V ADC reference. Q3 and the ADC are turned off as soon as the conversion finishes. The cell voltage barely moves over a few intervals, so a healthy cell is measured once every **5** wakes (button or interval). The percent is kept in SRAM, which Power-Down retains, and that cached value is what the corner readout shows on the four wakes in between. Boot and reset always measure. A failed conversion is retried on the next wake.

Two cases still measure every wake:

- The low-battery warning is already on the glass, so plugging in a charger clears it on the next wake.
- The last reading is still at or below 15% (~3.60 V), so the cutoff on the steep part of the curve is not missed.
