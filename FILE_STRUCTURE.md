# Double DAQ — File Structure

```
double_DAQ/
│
├── CMakeLists.txt              ← Shared build config; defines both targets
├── pico_sdk_import.cmake       ← Pico SDK locate/fetch (shared)
├── ws2812.pio                  ← NeoPixel PIO program (shared by both targets)
├── .gitignore
│
├── ARCHITECTURE.md             ← System overview, pin map, protocol description
├── FILE_STRUCTURE.md           ← This file
│
├── central/                    ── Central DAQ (SPI master, SD writer)
│   ├── logic_analyser.c        ← Main: SPI init, ACK/READY poll loop, SD write
│   ├── neopixel.c / .h         ← WS2812 driver (PIO1, GPIO16)
│   ├── flash_log.c / .h        ← Flash backup for captures
│   ├── sd_config.c / .h        ← SD card mount, config.txt, capture write
│   ├── hw_config.c             ← FatFS_SPI board config (SPI1 for SD card)
│   └── no-OS-FatFS-SD-SPI-RPi-Pico-master/  ← SD/FatFS library
│
├── peripheral/                 ── Peripheral DAQ (PIO capture, SPI slave)
│   ├── logic_analyser.c        ← Main: PIO capture, ACK/READY protocol, NeoPixel
│   ├── spi_central_config.c    ← SPI1 init/deinit, ACK/READY GPIO, blocking send
│   └── spi_central_config.h    ← API: init, deinit, raise/lower ACK, wait_for_ready
│
├── matlab/                     ── Post-processing scripts
│   ├── read_daq_bin.m
│   └── read_daq_minimal.m
│
└── build/                      ← CMake build output (gitignored)
    ├── peripheral_logic_analyzer.uf2   ← Flash to peripheral Pico
    ├── central_logic_analyzer.uf2      ← Flash to central Pico
    └── ...
```

## Source File Summary

### Shared

| File                   | Purpose                                          |
| ---------------------- | ------------------------------------------------ |
| `CMakeLists.txt`       | Defines both targets; links SDK + FatFS          |
| `ws2812.pio`           | PIO assembly for WS2812 NeoPixel LED             |
| `pico_sdk_import.cmake`| Locates the Raspberry Pi Pico SDK                |

### Central

| File              | Purpose                                                       |
| ----------------- | ------------------------------------------------------------- |
| `logic_analyser.c`| SPI0 master init, ACK/READY polling loop, SD/flash write      |
| `neopixel.c/.h`   | Non-blocking NeoPixel blink driver via PIO1                   |
| `flash_log.c/.h`  | Capture backup to on-chip flash; hex dump / erase over USB    |
| `sd_config.c/.h`  | SD mount, config.txt parse, run-directory management          |
| `hw_config.c`     | FatFS_SPI pin config: SPI1 MISO=8, MOSI=15, SCK=14, CS=13    |

### Peripheral

| File                   | Purpose                                                  |
| ---------------------- | -------------------------------------------------------- |
| `logic_analyser.c`     | PIO capture engine, DMA, ACK/READY transfer, NeoPixel    |
| `spi_central_config.c` | SPI1 slave init/deinit, blocking send, handshake GPIOs   |
| `spi_central_config.h` | Public API for SPI + ACK/READY protocol                  |