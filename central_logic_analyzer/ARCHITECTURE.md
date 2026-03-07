# PIO Logic Analyser – High-Level Architecture

## Block Diagram

```
┌──────────────────────────────────────────────────────────────┐
│  RP2040 MASTER (SPI bus controller + SD storage)              │
│                                                                │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  master_firmware.c                                     │    │
│  │  • Polls ACK pins — when one goes low, read that      │    │
│  │    peripheral via SPI                                  │    │
│  │  • Prepends metadata header to each capture            │    │
│  │  • Writes to SD via FatFs                              │    │
│  └──────┬──────────────────────────────────┬─────────────┘    │
│         │                                    │                  │
│         ▼                                    ▼                  │
│  ┌──────────────┐    ┌───────────────────────────────────┐    │
│  │  FatFs stack  │    │  Peripheral table                  │    │
│  │  (same as     │    │  periph[0]: CS=GPIO2, ACK=GPIO6   │    │
│  │   before)     │    │  periph[1]: CS=GPIO3, ACK=GPIO7   │    │
│  │              │    │  periph[2]: CS=GPIO4, ACK=GPIO8   │    │
│  └──────┬───────┘    └───────────────────────────────────┘    │
│         │                                                       │
│  Pin assignments:                                               │
│  GPIO0  ──► SPI0 SCK  (shared by all peripherals)              │
│  GPIO1  ──► SPI0 MOSI (master out — for commands if needed)    │
│  GPIO2  ──► CS_0  (peripheral 0)                                │
│  GPIO3  ──► CS_1  (peripheral 1)                                │
│  GPIO4  ──► CS_2  (peripheral 2)                                │
│  GPIO5  ──► SPI0 MISO (shared — only one talks at a time)      │
│  GPIO6  ──► ACK_0 (input, low = data ready)                    │
│  GPIO7  ──► ACK_1 (input)                                      │
│  GPIO8  ──► ACK_2 (input)                                      │
│  GPIO12-15 ► SPI1 + CS for SD card (same as before)            │
│  GPIO16 ──► NeoPixel                                            │
└──────────────────────────────────────────────────────────────┘
```

## Repo Structure (What’s Where)

| Path | Purpose |
|------|---------|
| `logic_analyser.c` | Main firmware: PIO capture, SD/flash write, USB commands |
| `hw_config.c` | Your board config: SPI pins, SD CS, accessors for the library |
| `CMakeLists.txt` | Build: links Pico SDK, FatFs_SPI, your app |
| `pico_sdk_import.cmake` | Locates/fetches Pico SDK |
| `read_daq_bin.m`, `read_daq_minimal.m` | MATLAB: read `.bin` captures, unpack bytes, optional plot |
| `ws2812.pio` | PIO program for NeoPixel (WS2812) |
| `no-OS-FatFS-SD-SPI-RPi-Pico-master/FatFs_SPI/` | SD + FatFs library (glue, ff15, sd_driver, src) |
| `no-OS-FatFS-SD-SPI-RPi-Pico-master/example/`, `simple_example/` | Demos (not used by your build) |

## Abbreviations

| Abbr. | Meaning |
|-------|---------|
| **ADC** | Analog-to-Digital Converter; here used as label for sample clock |
| **ARM** | Here: “armed” – capture enabled when ARM pin goes high |
| **CMake** | Build system: configures and generates Makefiles / Ninja |
| **CS** | Chip Select; active-low line to select SD card on SPI |
| **DMA** | Direct Memory Access; hardware moves data without CPU |
| **exFAT** | Extended FAT filesystem; supports large volumes |
| **FAT** | File Allocation Table; common filesystem (FAT12/16/32) |
| **FatFs** | Fat File System – ChaN’s embedded FAT/exFAT library |
| **GPIO** | General Purpose Input/Output; programmable pin |
| **LBA** | Logical Block Address; sector index on disk |
| **LFN** | Long File Name; extended filename support in FAT |
| **MATLAB** | Scripts run on PC, not on Pico |
| **MISO** | Master In, Slave Out; SPI line from device to master |
| **MOSI** | Master Out, Slave In; SPI line from master to device |
| **NeoPixel** | WS2812 RGB LED; status indicator |
| **PIO** | Programmable I/O; RP2040 state machines for precise timing |
| **RP2040** | Raspberry Pi Pico microcontroller |
| **RTC** | Real-Time Clock; used for file timestamps |
| **SCK** | Serial Clock; SPI clock line |
| **SD** | Secure Digital; memory card |
| **SM** | State Machine; PIO execution unit |
| **SPI** | Serial Peripheral Interface; 4-wire serial bus |
| **USB** | Used for stdio and `d`/`e` commands when flash capture exists |
