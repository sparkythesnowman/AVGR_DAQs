# PIO Logic Analyser – File Structure

```
pio_logic_analyser/
│
├── logic_analyser.c          ← Main firmware (capture, SD/flash, USB, NeoPixel)
├── hw_config.c               ← Board config: SPI + SD pin mapping
├── CMakeLists.txt            ← Build config: links app + FatFs_SPI
├── pico_sdk_import.cmake     ← Pico SDK locate/fetch
│
├── ws2812.pio                ← PIO program for NeoPixel LED
│
├── read_daq_bin.m            ← MATLAB: read .bin, unpack, optional analysis
├── read_daq_minimal.m        ← MATLAB: minimal read + plot
│
├── ARCHITECTURE.md           ← High-level architecture + abbreviations
├── FILE_STRUCTURE.md         ← This file
│
├── .vscode/                  ← IDE settings (launch, tasks, cmake-kits)
│
└── no-OS-FatFS-SD-SPI-RPi-Pico-master/
    │
    ├── FatFs_SPI/            ← SD + FatFs library (used by your build)
    │   ├── CMakeLists.txt
    │   ├── ff15/source/      ← FatFs core
    │   │   ├── ff.c, ff.h
    │   │   ├── diskio.c, diskio.h
    │   │   ├── ffconf.h
    │   │   ├── ffunicode.c, ffsystem.c
    │   │   └── ...
    │   ├── sd_driver/        ← SD over SPI
    │   │   ├── sd_card.c, sd_card.h
    │   │   ├── sd_spi.c, sd_spi.h
    │   │   ├── spi.c, spi.h
    │   │   ├── crc.c, crc.h
    │   │   ├── hw_config.h   ← Declares spi_get_*, sd_get_* (you implement in hw_config.c)
    │   │   └── demo_logging.c
    │   ├── src/              ← Glue + utilities
    │   │   ├── glue.c        ← disk_* → sd_card methods
    │   │   ├── ff_stdio.c
    │   │   ├── f_util.c
    │   │   ├── rtc.c
    │   │   └── my_debug.c
    │   └── include/
    │
    ├── example/              ← Demo apps (not used by pio_logic_analyser)
    ├── simple_example/
    └── dynamic_config_example/
```

## Build Output (typical)

```
build/
├── pio_logic_analyser.uf2    ← Flashed to Pico
├── pio_logic_analyser.elf
└── ...
```
