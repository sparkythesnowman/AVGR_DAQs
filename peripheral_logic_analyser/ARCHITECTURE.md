# PIO Logic Analyser (Peripheral) – Architecture

## Overview

This Pico acts as a **peripheral/slave** in a dual-DAQ system. It captures 8-bit parallel data via PIO + DMA, then sends the captured buffer to a **central DAQ** over SPI.

## Block Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                    RASPBERRY PI PICO (RP2350)                    │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │  logic_analyser.c                                          │  │
│  │  • PIO0: 8-bit capture (GPIO0-7) + clock out (GPIO9)      │  │
│  │  • DMA: PIO RX FIFO → RAM buffer                          │  │
│  │  • Trigger (GPIO11) + Arm (GPIO29) control                 │  │
│  │  • NeoPixel status LED (PIO1, GPIO16)                      │  │
│  └──────────────────────┬─────────────────────────────────────┘  │
│                         │                                        │
│  ┌──────────────────────┴─────────────────────────────────────┐  │
│  │  spi_central_config.c / .h                                 │  │
│  │  • SPI0 slave: sends header + data to central DAQ          │  │
│  │  • ACK handshake pin for transfer confirmation             │  │
│  └────────────────────────────────────────────────────────────┘  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘

GPIO Pin Map:
  GPIO0-7  ── 8-bit data bus (captured signals)
  GPIO9    ── ADC_CLK_PIN (sample clock output)
  GPIO11   ── TRIGGER_PIN (start capture on high)
  GPIO16   ── NeoPixel (WS2812 status LED) / SPI0 MISO
  GPIO17   ── SPI0 CS (central selects us)
  GPIO18   ── SPI0 SCK
  GPIO19   ── SPI0 MOSI
  GPIO20   ── ACK_PIN (handshake with central)
  GPIO29   ── ARM_PIN (arm capture)
```

## Data Flow

```
External signals ──► PIO0 (sample + clock) ──► DMA ──► RAM buffer
                                                          │
                                                          ▼
                                               write_capture_to_spi()
                                                          │
                                               ┌──────────┴──────────┐
                                               │ 1. Send header      │
                                               │ 2. Send data chunks │
                                               │ 3. Wait for ACK     │
                                               └─────────────────────┘
                                                          │
                                                     SPI0 bus
                                                          │
                                                          ▼
                                                   Central DAQ
```

## Abbreviations

| Abbr.        | Meaning                                                    |
| ------------ | ---------------------------------------------------------- |
| **ACK**      | Acknowledge; GPIO handshake confirming transfer success     |
| **ADC**      | Analog-to-Digital Converter; here used as sample clock label|
| **ARM**      | "Armed" – capture enabled when ARM pin goes high           |
| **CS**       | Chip Select; active-low line to select device on SPI       |
| **DMA**      | Direct Memory Access; hardware moves data without CPU      |
| **GPIO**     | General Purpose Input/Output; programmable pin             |
| **MISO**     | Master In, Slave Out; SPI data from peripheral to master   |
| **MOSI**     | Master Out, Slave In; SPI data from master to peripheral   |
| **NeoPixel** | WS2812 RGB LED; status indicator                           |
| **PIO**      | Programmable I/O; RP2350 state machines for precise timing |
| **SCK**      | Serial Clock; SPI clock line                               |
| **SM**       | State Machine; PIO execution unit                          |
| **SPI**      | Serial Peripheral Interface; 4-wire serial bus             |