# Double DAQ — What the code does today vs. what you described

*Read time ~25 min. Nothing in the firmware has been changed. This is a description of the current state plus where it diverges from your spec.*

---

## TL;DR

The firmware works end-to-end for the happy path: a peripheral captures 8 channels at 100 MHz into RAM via PIO+DMA, and the central pulls each capture over a shared SPI bus and writes a `.bin` per peripheral to SD. **The capture engine and the SD write path are real and match your intent.**

The **coordination and status layers are not what you described.** Specifically:

1. There is **no boot-time handshake** with the peripherals — the central never checks which boards are present or alive.
2. There is **no "empty"/installed concept** — the dump loop always reads all 8 demux positions, so an absent board produces a 128 KB junk file (and still shows **green**).
3. `config.txt` is read, but only a single `PREFIX=` line — **not** per-peripheral names, **not** a central name, **not** "empty".
4. The on-boot LED is **orange → red (no SD) / blue (SD OK)**, not orange → red/green.
5. Addressing is done with a **74HC138 3-to-8 demux + a routed chip-select pulse**, not the ACK/READY GPIO handshake the docs describe (that code is all commented out).
6. The two markdown docs (`ARCHITECTURE.md`, `FILE_STRUCTURE.md`) are **stale** — they describe a 2-board, ACK/READY design with different pin maps and filenames.

Also: roughly **two-thirds of both `main()` files are commented-out diagnostic blocks**. The "real" program is small and lives at the bottom of each file.

---

## 1. How a run actually plays out

### The real coordination scheme (this is the key thing to understand)

The system does **not** use the ACK/READY handshake in `ARCHITECTURE.md`. It uses three mechanisms:

- **ARM is broadcast over the SPI MOSI line.** Before SPI is initialised, the central bit-bangs its MOSI pin (`GPIO3`) HIGH for 1 s. Every peripheral reads that same line as its "ARM" input (`GPIO12`, which is its MOSI/RX). One wire, all boards armed at once.
- **TRIGGER is a hardware-gated GPIO.** Each peripheral's PIO program literally starts with `WAIT for GPIO11 HIGH`, so sampling can't begin until the trigger fires — and the state machine isn't even enabled until after ARM. So a pre-arm trigger does nothing, which matches your spec.
- **Board selection is a demux + chip-select pulse.** The central drives a 74HC138: address bits `A/B/C` (`GPIO27/28/29`), enable `G1` (`GPIO26`, always high), and uses the **SPI0 hardware CS pin (`GPIO1`) as the demux's active-low enable (G2)**. To talk to peripheral *n*, it sets the address to *n* and pulses CS low. The demux routes that low to exactly one peripheral's CS pin (`GPIO13`). That peripheral wakes up, brings up its SPI slave, and streams its buffer. Because `GPIO1` doubles as the SPI hardware CS, it stays asserted to the selected board for the whole transfer, so only that board drives MISO.

```
ARM (MOSI held high 1s) ──────────────► all peripherals arm
TRIGGER (GPIO high) ──────────────────► PIO starts sampling (per board, hardware-gated)
                                         each board fills its own 128 KB RAM buffer

then, for v = 0..7:
  central sets demux A/B/C = v, pulses CS low ──► peripheral v's CS goes low
  central spi_read_blocking(131072)          ◄── peripheral v spi_write_blocking(131072)
  central writes RUN_xxxxx/<prefix>_..._v.bin to SD
```

### Central (`central/logic_analyzer.c`), boot → loop

1. USB/stdio, 5 s countdown.
2. `neopixel_init()` → starts a **dim orange blink** (non-blocking, timer-serviced).
3. `rtc_init()` → brings up an **MCP7940N RTC over I2C** (`SDA=8, SCL=9`). If the RTC isn't already running, it seeds the time from the firmware's compile time (`__TIME__`) and starts the oscillator.
4. `sd_init()` → mount SD. **Fail → solid red + halt forever. Success → dim blue** ("SD ready, waiting").
5. `demux_init()`, then the forever loop:
   - `init_run_directory()` → finds the highest existing `RUN_#####` folder, makes the next one, writes a `meta.txt` into it. This also calls `load_config_from_sd()` (see §3).
   - Set MOSI low, CS high (idle).
   - **Wait for ARM input** (`GPIO5` high) → grey/white blink, then pulse MOSI high 1 s (the ARM broadcast), LED off.
   - **Wait for TRIGGER input** (`GPIO4` high) → read RTC timestamp, grey/white blink.
   - `sleep_ms(3000)` to let peripherals finish capturing + bit-reversing.
   - **Dump loop `v = 0..7`:** demux-select v, pulse CS, `spi_master_init()`, `spi_read_blocking(131072)` into `rx_buf`, `spi_deinit`, write to SD. Green blink per board on a successful write.
   - `sd_unmount()`, `cap_cnt++`, repeat.

### Peripheral (`peripheral/logic_analyzer.c`), boot → loop

1. USB/stdio, 3 s countdown.
2. `set_sys_clock_khz(200000)` → runs the chip at **200 MHz**.
3. `neopixel_init()`, allocate the 128 KB capture buffer, give DMA top bus priority.
4. Load the 2-instruction PIO sampler (sample 8 pins + toggle the ADC clock on sideset). With sys=200 MHz and clkdiv≈1.0, the 2-cycle loop yields a **100 MHz sample rate**; the ADC clock comes out on `GPIO9`.
5. Forever loop:
   - Re-arm DMA + PIO; PIO's first instruction is `WAIT GPIO11 (trigger)`.
   - **Wait for ARM** (`GPIO12` high) → **blue while waiting**, grey blink when armed, LED off.
   - Start DMA + enable PIO → samples flow into RAM until the buffer is full → grey blink "capture complete".
   - **Bit-reverse every byte** in the buffer (the ADC's data bus is wired bit-reversed relative to `GPIO0–7`, so this puts bits back in order).
   - **Wait for CS low** (`GPIO13`), then `spi_slave_init()` and `spi_write_blocking(131072)` to the central, `spi_slave_deinit()`, green blink "sent", repeat.

A detail worth knowing: just before each transfer the peripheral reconfigures `clk_peri` to 125 MHz; the comment says this fixes a byte frame-shift in the SPI stream.

---

## 2. Your 6 stages vs. the code

| Your stage | What you described | What the code does | Match |
|---|---|---|---|
| **1. Boot / SD** | Flash **orange** while searching; **red** if no SD; **green** if mounted | Orange blink on boot ✓; **red** + halt if no SD ✓; **dim blue** (not green) if mounted | ⚠️ color |
| **2. Config** | Read `config.txt` for **central + each peripheral name**; support **"empty"**; default names `default_central_..._peripheral_<CS>` if no file | Reads `config.txt` but only a single **`PREFIX=`** string. No names, no central name, no "empty". Default prefix is just `CAP_`. No-file → solid orange; file-present → no status change | ❌ |
| **3. Handshake each peripheral** | SPI handshake per board, **skip empties**, **green/red per board** | **No handshake at all.** No presence check, no skipping, no per-board status. (Central has one LED; per-board state can only live on each peripheral's own LED.) | ❌ |
| **4. Wait → ARM** | Wait for arm; on arm **all LEDs flash white then off** | Central & peripheral wait for ARM, blink **grey (≈dim white) once, then off** ✓ | ✅ ~ |
| **5a. Armed → TRIGGER** | Wait for trigger; **pre-arm trigger does nothing** | PIO is enabled only after ARM and is hardware-gated on the trigger, so pre-arm triggers are ignored ✓ (level-triggered, no edge detect) | ✅ |
| **5b. Collect** | Each peripheral captures the ADC's 8-bit output via DMA | PIO+DMA, 8 ch, 100 MHz, 131072 samples ✓ | ✅ |
| **6. Dump** | Central reads each **installed** board **in order**, writes `.bin` named per config/default | Reads all 8 in order ✓, writes per-board `.bin` into `RUN_#####/` ✓, but **never skips empties** and names use the global prefix + timestamp + index, **not config names** | ⚠️ |

---

## 3. `config.txt` — what's actually parsed

`load_config_from_sd()` (in `sd_config.c`) only does this:

- **No SD** → blue, state `NO_SD`.
- **SD but no `config.txt`** → **solid orange**, state `NO_FILE`. (The old "fall back to flash config" path is removed/commented.)
- **`config.txt` present** → reads lines, and the **only** key it acts on is `PREFIX=`. `SAMPLE_HZ=` parsing is commented out. There is no handling for board names or "empty".
  - Latent issue: in this success branch the green/"match" LED and the `g_config_state` update are **commented out**, so after a successful config read `g_config_state` is still left at its initial `NO_SD` value and the LED stays blue.

Filenames are built as:

```
0:RUN_#####/<PREFIX><cap_cnt>_<day>_<hh>_<mm>_<ss>_<periphIndex>.bin
```

Two caveats on the timestamp: the RTC has **no battery** wired yet (per the code comment), so time is reseeded from compile time on each boot; and `rtc_get_time()` fills the `day` field with **day-of-week (1–7)**, while year/month are always 0 — so the `<day>` in the filename is the weekday, not the date.

---

## 4. LED colors actually in use

| Board | Color | When |
|---|---|---|
| Central | dim orange (blink) | boot / init |
| Central | **red (solid)** | SD mount failed → halt |
| Central | **dim blue (solid)** | SD mounted, waiting (your spec wanted green here) |
| Central | solid orange | SD OK but no `config.txt` |
| Central | grey/white (1 blink) | ARM received; TRIGGER received |
| Central | blue → green / red | during SD write → success / failure |
| Peripheral | dim orange (blink) | boot |
| Peripheral | blue (solid) | waiting for ARM |
| Peripheral | grey/white (1 blink) | armed; capture complete |
| Peripheral | green (1 blink) | buffer sent to central |

Peripheral **red error states are all commented out**, so a peripheral never signals a fault.

---

## 5. Other things that differ or are worth flagging

These are observations only — no fixes applied.

- **A missing/silent board still reports success.** With no handshake, the central pulses an empty demux slot, reads 128 KB of idle-bus bytes, and writes them as a normal file with a green blink. There's no way today to tell a real capture from a dead slot. This is the most important gap relative to your "if any board is red, you know it's misbehaving" goal.
- **Stale docs.** `ARCHITECTURE.md` / `FILE_STRUCTURE.md` describe a **two-peripheral** system using an **ACK/READY GPIO handshake**, files named `logic_analyser.c` / `spi_central_config.c` / `hw_config.c`, and an SD pin map of `MISO=8/SCK=14/MOSI=15`. The actual code is **8 peripherals via demux**, files `logic_analyzer.c` / `spi_slave_config.c`, SD on `SCK=10/MOSI=11/MISO=12/CS=13`, and `hw_config` merged into `sd_config.c`. Treat the two `.md` files as out of date.
- **Pin-map inconsistencies inside the code.** The central's SPI0 SCK is `#define SPI_SCK_PIN 6` while the file's header comment says `SCK=GPIO2` (the `#define`, 6, is what compiles). The RTC uses `GPIO8/9`, which the stale doc lists as the SD MISO pin — so don't trust the doc's pin table.
- **Vestigial handshake pins.** The peripheral still defines `ACK_PIN = 26` and a `wait_for_ack()` helper, but the active path never uses them; selection is entirely via the demux-routed CS.
- **Dead code volume.** `central/logic_analyzer.c` is 964 lines but the live program is ~lines 792–964; the rest are commented diagnostic harnesses (pin toggles, SPI/DMA/SD diagnostics, RTC test). Same pattern in the peripheral. Worth pruning for readability, but harmless at runtime.
- **`flash_log` is largely stubbed.** The flash-backup-on-SD-failure paths are commented out; the central currently just goes red and returns on a write failure.
- **RTC baud comment mismatch.** `RTC_BAUD_HZ` is set to 200 kHz but the comment says 400 kHz (cosmetic).

---

## 6. Where to look (and what to ignore)

| Read this | Why |
|---|---|
| `peripheral/logic_analyzer.c` (bottom ~lines 215→end) | the real capture + send loop |
| `peripheral/capture.c` / `.h` | PIO sampler + bit-packing math |
| `peripheral/spi_slave_config.c` | SPI slave bring-up, pin map |
| `central/logic_analyzer.c` (bottom ~lines 792→end) | the real arm/trigger/dump loop + demux |
| `central/sd_config.c` | mount, `config.txt`, run dirs, file naming |
| `central/rtc.c` / `.h` | MCP7940N timestamping |
| `*/neopixel.c` | LED states |
| Ignore: top ~600 lines of each `main()` | commented-out diagnostics |
| Ignore: `central/no-OS-FatFS-SD-SPI-RPi-Pico-master/` | third-party SD/FatFS library |
| Treat as stale: `ARCHITECTURE.md`, `FILE_STRUCTURE.md` | describe an older 2-board design |
