# PIO Logic Analyser (Peripheral) – File Structure

```
peripheral_logic_analyser/
│
├── logic_analyser.c          ← Main firmware: PIO capture, DMA, SPI send, NeoPixel
├── spi_central_config.c      ← SPI driver: init, send, ACK handshake
├── spi_central_config.h      ← Header for SPI driver
├── CMakeLists.txt            ← Build config: links Pico SDK + hardware libs
├── pico_sdk_import.cmake     ← Pico SDK locate/fetch
│
├── ws2812.pio                ← PIO program for NeoPixel LED (compiled by CMake)
│
├── ARCHITECTURE.md           ← High-level architecture + pin map
├── FILE_STRUCTURE.md         ← This file
│
├── .vscode/                  ← IDE settings (launch, tasks, cmake-kits)
│
└── build/                    ← Build output (gitignored)
    ├── pio_logic_analyser.uf2    ← Flash this to the Pico
    ├── pio_logic_analyser.elf
    └── ...
```

## Source File Overview

| File                    | Purpose                                              |
| ----------------------- | ---------------------------------------------------- |
| `logic_analyser.c`      | PIO capture engine, DMA setup, capture loop, NeoPixel driver, main() |
| `spi_central_config.c`  | SPI0 init, chunked send, ACK handshake via GPIO      |
| `spi_central_config.h`  | Public API: init, send, ready, wait_for_ack          |
| `ws2812.pio`            | PIO assembly for driving WS2812 NeoPixel LED         |
| `CMakeLists.txt`        | Links: pico_stdlib, hardware_pio, hardware_dma, hardware_clocks, hardware_spi |