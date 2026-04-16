# Double DAQ — Architecture

## Overview

Two-board system. A **central** RP2040 Pico acts as SPI master, SD-card writer, and coordinator. Up to two **peripheral** RP2040 Picos each capture 8-bit parallel data via PIO + DMA, then stream the buffer to the central over a shared SPI0/SPI1 bus.

## Board Roles

| Board      | SPI role | Primary job                                         |
| ---------- | -------- | --------------------------------------------------- |
| Central    | Master   | Poll ACK pins, receive captures, write to SD card   |
| Peripheral | Slave    | PIO capture, deferred SPI init, stream on handshake |

## ACK / READY Handshake Protocol

```
Peripheral                           Central
──────────                           ───────
Capture complete
  → raise ACK (HIGH)  ─────────────► sees ACK HIGH
                                       → raise READY (HIGH)
sees READY HIGH ◄────────────────────
  → spi_central_init()
  → spi_write_blocking(131072 B)
                      ─────────────► spi_read_blocking(131072 B)
Transfer done
  → lower ACK (LOW)   ─────────────► sees ACK LOW
  → spi_central_deinit()              → lower READY (LOW)
                                       → write to SD
                                       → poll next peripheral
```

Key properties:
- SPI hardware is **not initialised** on the peripheral until READY goes HIGH.
- Only one peripheral will have SPI initialised at a time; the shared CS line is hardware-managed but harmless because the inactive peripheral's SPI is off.

## GPIO Pin Map

### Central (RP2040)

| GPIO | Function            |
| ---- | ------------------- |
| 0    | SPI0 RX (MISO)      |
| 1    | SPI0 CSn (hardware) |
| 2    | SPI0 SCK            |
| 3    | SPI0 TX (MOSI)      |
| 8    | SPI1 MISO (SD)      |
| 13   | SPI1 CS (SD)        |
| 14   | SPI1 SCK (SD)       |
| 15   | SPI1 MOSI (SD)      |
| 16   | NeoPixel (WS2812)   |
| 26   | Periph 1 READY out  |
| 27   | Periph 1 ACK in     |
| 28   | Periph 2 READY out  |
| 29   | Periph 2 ACK in     |

### Peripheral (RP2040)

| GPIO  | Function                        |
| ----- | ------------------------------- |
| 0–7   | 8-bit capture data bus          |
| 9     | ADC clock output (PIO sideset)  |
| 11    | Trigger pin (capture start)     |
| 12    | SPI1 RX (MOSI from central)     |
| 13    | SPI1 CSn (from central)         |
| 14    | SPI1 SCK (from central)         |
| 15    | SPI1 TX (MISO to central)       |
| 16    | NeoPixel (WS2812)               |
| 27    | READY in (from central)         |
| 28    | ACK out (to central)            |
| 29    | ARM pin (arm capture)           |

## Data Flow

```
External signals ──► PIO0 (sample + clock) ──► DMA ──► RAM buffer (131072 B)
                                                               │
                                                  write_capture_to_spi()
                                                               │
                                                  ACK/READY handshake
                                                               │
                                                  spi_write_blocking (peripheral)
                                                  spi_read_blocking  (central)
                                                               │
                                                        SD card write
```

## Abbreviations

| Abbr.   | Meaning                                                 |
| ------- | ------------------------------------------------------- |
| ACK     | Acknowledge; peripheral signals data ready / done       |
| ADC     | Analog-to-Digital Converter (used as sample clock label)|
| ARM     | Armed — capture enabled when ARM pin goes high          |
| CS      | Chip Select; active-low, hardware-managed by SPI0       |
| DMA     | Direct Memory Access; fills RAM buffer from PIO FIFO    |
| GPIO    | General Purpose Input/Output                            |
| MISO    | Master In Slave Out                                     |
| MOSI    | Master Out Slave In                                     |
| NeoPixel| WS2812 RGB LED status indicator                         |
| PIO     | Programmable I/O; RP2040 state machines                 |
| READY   | Central signals it is ready to receive a transfer       |
| SCK     | Serial Clock                                            |
| SM      | State Machine (PIO)                                     |
| SPI     | Serial Peripheral Interface                             |