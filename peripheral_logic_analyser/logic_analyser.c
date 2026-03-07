/**
 * PIO logic analyser with ADC clock (sideset) + flash logging
 * - Clock output on GPIO9 (ADC_CLK_PIN)
 * - 8-bit sample bus on GPIO 0..7

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/clocks.h"

#include "spi_central_config.h"

#include "ws2812.pio.h"   // generated from ws2812.pio by CMake

// ---------- Config ----------
const uint CAPTURE_PIN_BASE   = 0;    // D0..D7 -> GPIO0..GPIO7
const uint CAPTURE_PIN_COUNT  = 8;
const uint CAPTURE_N_SAMPLES  = 131072; // logical samples per pin (64 kB)
const uint TRIGGER_PIN        = 11;
const uint ARM_PIN            = 29;
const bool TRIGGER_LEVEL      = true; // trigger on high

#define ADC_CLK_PIN         9   // ADC clock output pin

#define DEFAULT_SAMPLE_HZ   (100u*1000000u)

typedef struct {
    uint32_t magic;         // 'LAC1' = 0x3143414C
    uint16_t version;       // 1
    uint16_t hdr_len;       // sizeof(header)
    uint32_t sample_count;  // logical samples per pin
    uint32_t pin_base;
    uint32_t pin_count;
    uint32_t sample_hz;     // effective sample rate
    uint32_t trigger_pin;
    uint32_t trigger_level; // 0/1
    uint32_t word_count;    // 32-bit words stored following header
    uint32_t reserved0;
} logic_log_header_t;

static uint32_t g_capture_index =       0; // counter
static uint32_t g_target_sample_hz  =   DEFAULT_SAMPLE_HZ;      
static const uint32_t LOG_MAGIC =       0x3143414Cu; // 'LAC1'
static float g_clkdiv =                 1.0f;    // actual SM clkdiv used

// ---------- Onboard NeoPixel (WS2812) on GPIO16 using PIO1 (lines 54-198) ----------
#define NEOPIXEL_PIO      pio1
#define NEOPIXEL_SM       0
#define NEOPIXEL_PIN      16
#define NEOPIXEL_IS_RGBW  false

static inline void ws2812_program_init(PIO pio, uint sm, uint offset,
                                       uint pin, float freq, bool rgbw) {
    // Init the data pin
    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);

    // Base config from generated ws2812.pio.h
    pio_sm_config c = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin);

    // WS2812: 24 (RGB) or 32 (RGBW) bits, MSB first, autopull
    sm_config_set_out_shift(&c, false, true, rgbw ? 32 : 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // PIO bit clock = freq * (T1+T2+T3)
    const float cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3; // 10
    float div = (float)clock_get_hz(clk_sys) / (freq * cycles_per_bit);
    sm_config_set_clkdiv(&c, div);

    // Apply and start
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static inline uint32_t pack_grb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

static inline void neopixel_write_rgb_raw(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t grb = pack_grb(r, g, b);
    pio_sm_put_blocking(NEOPIXEL_PIO, NEOPIXEL_SM, grb << 8u);
}

static inline void neopixel_off_raw(void) {
    neopixel_write_rgb_raw(0, 0, 0);
}

typedef struct {
    bool     active;
    bool     led_on;
    uint8_t  r, g, b;
    uint32_t on_ms;
    uint32_t off_ms;
    uint32_t blinks_left;      // full on+off cycles remaining
    uint32_t next_change_ms;   // next time (ms since boot) to toggle
} neopixel_blink_state_t;

static neopixel_blink_state_t g_np_blink = {0};

// Call this frequently (e.g. in while loops) to advance the blink pattern.
// Completely non-blocking.
void neopixel_blink_service(void)
{
    if (!g_np_blink.active) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now - g_np_blink.next_change_ms) < 0) {
        return; // not time yet
    }

    if (!g_np_blink.led_on) {
        // Turn LED on
        neopixel_write_rgb_raw(g_np_blink.r, g_np_blink.g, g_np_blink.b);
        g_np_blink.led_on = true;
        g_np_blink.next_change_ms = now + g_np_blink.on_ms;
    } else {
        // Turn LED off; finished one full blink
        neopixel_off_raw();
        g_np_blink.led_on = false;

        if (g_np_blink.blinks_left > 0) {
            g_np_blink.blinks_left--;
        }

        if (g_np_blink.blinks_left == 0) {
            // Sequence complete
            g_np_blink.active = false;
        } else {
            g_np_blink.next_change_ms = now + g_np_blink.off_ms;
        }
    }
}


// Timer used to service non-blocking NeoPixel blinking
static repeating_timer_t g_np_timer;

// Timer callback: runs neopixel_blink_service() periodically
static bool neopixel_blink_timer_cb(repeating_timer_t *rt) {
    neopixel_blink_service();
    return true;    // keep repeating
}


static inline void neopixel_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
    // External manual color set: cancel any ongoing blink sequence
    g_np_blink.active = false;
    neopixel_write_rgb_raw(r, g, b);
}

static inline void neopixel_off(void) {
    g_np_blink.active = false;
    neopixel_off_raw();
}


// Start a non-blocking multi-blink sequence.
// - Blinks 'count' times with color (r,g,b)
// - Each blink is on for on_ms, off for off_ms
// - Returns immediately; must call neopixel_blink_service() regularly.
void neopixel_blink_multiple_start(uint8_t r, uint8_t g, uint8_t b,
                                   uint32_t on_ms, uint32_t off_ms,
                                   uint32_t count)
{
    if (count == 0) {
        // Stop any existing pattern
        g_np_blink.active = false;
        neopixel_off();
        return;
    }

    g_np_blink.active      = true;
    g_np_blink.led_on      = false;
    g_np_blink.r           = r;
    g_np_blink.g           = g;
    g_np_blink.b           = b;
    g_np_blink.on_ms       = on_ms;
    g_np_blink.off_ms      = off_ms;
    g_np_blink.blinks_left = count;
    g_np_blink.next_change_ms = to_ms_since_boot(get_absolute_time());  // toggle ASAP
}



static inline void neopixel_blink_once(uint8_t r, uint8_t g, uint8_t b, uint32_t ms) {
    neopixel_set_rgb(r, g, b);
    sleep_ms(ms);
    neopixel_off();
}

// ---------- Helpers (lines 212-224) ----------

static inline uint bits_packed_per_word(uint pin_count) {
    const uint SHIFT_REG_WIDTH = 32;
    return SHIFT_REG_WIDTH - (SHIFT_REG_WIDTH % pin_count);
}

static inline uint32_t words_for_samples(uint32_t samples, uint32_t pin_count) {
    uint32_t rec_bits = bits_packed_per_word(pin_count);
    uint32_t total_bits = samples * pin_count;
    return (total_bits + rec_bits - 1) / rec_bits;
}

// ---------- SPI TRANSFER (lines 213-249) ----------

static bool write_capture_to_spi(const uint32_t *cap_words,
                                uint32_t word_count,
                                uint32_t sample_hz)
{
    // 1. Init SPI (if not done yet)
    if(!spi_central_ready()){
        spi_central_init();
    }

    //send header so central knows what's coming
    logic_log_header_t header = {0}; // start default
    header.version = 1;
    header.hdr_len = sizeof(logic_log_header_t);
    header.magic = LOG_MAGIC;
    header.sample_count = CAPTURE_N_SAMPLES;
    header.pin_base = CAPTURE_PIN_BASE;
    header.pin_count = CAPTURE_PIN_COUNT;
    header.sample_hz = sample_hz;
    header.word_count = word_count;
    spi_central_send(&header, sizeof(header));

    // send capture data in chunks
    #define SPI_CHUNK_SIZE  256 //bytes per transfer
    uint8_t *src = (uint8_t *)cap_words;
    uint32_t total_bytes = word_count * 4;

    uint32_t offset = 0;
    for (offset = 0; offset < total_bytes; offset += SPI_CHUNK_SIZE) {
        uint32_t chunk_len = MIN(SPI_CHUNK_SIZE, total_bytes - offset);  // if # bytes left < chunk size 
        spi_central_send(src + offset, chunk_len);
    }
    if (!wait_for_ack(2000)) return false; // set timeout is 2s
    //add in errors/communication abt success
    return true;
}

// ---------- PIO: clock + sample via sideset (2 instructions) (lines 251-298) ----------
static inline uint clk_sample_program_load(PIO pio) {
    // Loop:
    //   in  pins,8  side 1   ; clock HIGH (rising) + sample
    //   nop        side 0    ; clock LOW (falling)
    uint16_t prog[2];
    prog[0] = (uint16_t)(pio_encode_in(pio_pins, 8) | pio_encode_sideset(1, 1));
    prog[1] = (uint16_t)(pio_encode_nop()         | pio_encode_sideset(1, 0));

    struct pio_program p = {
        .instructions = (const uint16_t*)prog,
        .length = 2,
        .origin = -1
    };
    return pio_add_program(pio, &p);
}

static inline void clk_sample_sm_init(PIO pio, uint sm, uint offset,
                                      uint clk_pin, uint data_pin_base,
                                      float clkdiv) {
    pio_sm_config c = pio_get_default_sm_config();

    // 1-bit sideset on clk_pin
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_sideset_pins(&c, clk_pin);

    // IN reads the 8-bit data bus
    sm_config_set_in_pins(&c, data_pin_base);

    // Autopush each 32 bits: 4 samples per 32-bit word (8 bits per sample)
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    sm_config_set_wrap(&c, offset, offset + 1);
    sm_config_set_clkdiv(&c, clkdiv);
    g_clkdiv = clkdiv;

    // Give PIO control + directions
    gpio_set_function(clk_pin, GPIO_FUNC_PIO0);
    pio_sm_set_consecutive_pindirs(pio, sm, clk_pin, 1, true); // clock output

    for (uint i = 0; i < CAPTURE_PIN_COUNT; ++i) {
        gpio_set_function(data_pin_base + i, GPIO_FUNC_PIO0);
        gpio_set_dir(data_pin_base + i, GPIO_IN);
    }

    pio_sm_init(pio, sm, offset, &c);
}

// ---------- Optional human-readable preview ----------
// void print_capture_buf(const uint32_t *buf, uint pin_base, uint pin_count, uint32_t n_samples) {
//     printf("Capture:\n");
//     uint record_size_bits = bits_packed_per_word(pin_count);
//     for (uint pin = 0; pin < pin_count; ++pin) {
//         printf("%02d: ", pin + pin_base);
//         for (uint32_t sample = 0; sample < n_samples; ++sample) {
//             uint bit_index = pin + sample * pin_count;
//             uint word_index = bit_index / record_size_bits;
//             uint word_mask = 1u << (bit_index % record_size_bits + 32 - record_size_bits);
//             printf(buf[word_index] & word_mask ? "-" : "_");
//         }
//         printf("\n");
//     }
// }

// ---------- Main ----------
int main() {

    // ---------- INIT (319-399) ----------

    stdio_init_all();
    // Wait for serial monitor to connect (countdown so user can open terminal)
    for (int i = 5; i > 0; i--) {
        sleep_ms(1000);
        if (stdio_usb_connected()) break;
    }
    printf("\n=== PIO Logic Analyser ===\n");
    printf("sys_clk = %u Hz\n", clock_get_hz(clk_sys));

    // init NeoPixel on GPIO16 using PIO1
    uint np_offset = pio_add_program(NEOPIXEL_PIO, &ws2812_program);
    ws2812_program_init(NEOPIXEL_PIO, NEOPIXEL_SM, np_offset,
                        NEOPIXEL_PIN, 800000.0f, NEOPIXEL_IS_RGBW);

    // Start a periodic timer to service non-blocking blinking
    // -5 ms => call every 5 ms, starting immediately
    add_repeating_timer_ms(-5, neopixel_blink_timer_cb, NULL, &g_np_timer);

    // Dim orange blink to show startup 
    neopixel_blink_multiple_start(15, 2, 0, 50, 50, 10000);


    // ---------- READY TO TRANSFER TO CENTRAL ----------
    spi_central_init(); // init to central DAQ

    cancel_repeating_timer(&g_np_timer); // stop blinking, no more cpu interrupts

    printf("PIO DAQ: Initializing\n");
    set_sys_clock_khz(200000, true);

    printf("Config: SAMPLE_HZ=%u\n",
           g_target_sample_hz);

    // ---------- Common setup (done once) ----------

    // Buffer sizing for N logical samples on 8 pins
    uint32_t buf_size_words = words_for_samples(CAPTURE_N_SAMPLES, CAPTURE_PIN_COUNT);
    uint32_t *capture_buf = (uint32_t *)malloc(buf_size_words * sizeof(uint32_t));
    hard_assert(capture_buf);

    // Prioritize DMA on the bus
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                            BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    PIO pio = pio0;
    uint sm  = 0;

    // ---------- PIO sample clock setup ----------
    // Sample/clock rate: f = clk_sys / (2 * clkdiv)
    uint32_t f_sys = clock_get_hz(clk_sys);
    float clkdiv = (float)f_sys / (2.0f * (float)g_target_sample_hz);
    g_clkdiv = clkdiv;

    // Load PIO program and init SM (clock + sample via sideset)
    uint prog_off = clk_sample_program_load(pio);
    clk_sample_sm_init(pio, sm, prog_off, ADC_CLK_PIN, CAPTURE_PIN_BASE, clkdiv);

    // Trigger pin
    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_IN);
    gpio_pull_down(TRIGGER_PIN);

    // Arm pin
    gpio_init(ARM_PIN);
    gpio_set_dir(ARM_PIN, GPIO_IN);
    gpio_pull_down(ARM_PIN); // initially pulled down

    // DMA channel setup (config reused each capture)
    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));

    // Effective sample rate (same every run unless you change clkdiv)
    uint32_t sample_hz = (uint32_t)((float)f_sys / (2.0f * g_clkdiv) + 0.5f);
    printf("Configured sample rate ~%u Hz (clkdiv=%.3f)\n", sample_hz, clkdiv);

    // ---------- Repeating capture loop ----------
    while (true) {
    
        // Configure DMA for this capture
        dma_channel_configure(dma_chan, &c,
            capture_buf,        // dest buffer in RAM
            &pio->rxf[sm],      // src: PIO RX FIFO
            buf_size_words,     // number of 32-bit words
            false               // don't start yet
        );

        // Prepare SM: clear FIFOs, restart, insert wait-for-trigger
        pio_sm_set_enabled(pio, sm, false);
        pio_sm_clear_fifos(pio, sm);
        pio_sm_restart(pio, sm);
        pio_sm_exec(pio, sm, pio_encode_wait_gpio(TRIGGER_LEVEL, TRIGGER_PIN));

        // ----- Wait for arming pin -----
        while (!gpio_get(ARM_PIN)) {
            tight_loop_contents();
        }
        neopixel_blink_once(50, 50, 50, 500);   // yellow blink = ARMED

        
        printf("Arming trigger for capture %lu...\n",
               (unsigned long)g_capture_index);
        g_capture_index = g_capture_index + 1;

        // Start DMA and then enable SM; SM starts clock+sampling on trigger
        dma_channel_start(dma_chan);
        pio_sm_set_enabled(pio, sm, true);

        // Block until DMA has filled the buffer
        dma_channel_wait_for_finish_blocking(dma_chan);

        // Stop the SM until next run
        pio_sm_set_enabled(pio, sm, false);

        // Write this capture to spi central
        bool wrote_to_spi = write_capture_to_spi(capture_buf, buf_size_words, sample_hz);
        sleep_ms(3000);
        if (wrote_to_spi) {
            neopixel_set_rgb(0, 100, 0); //green = sent
        } else {
            neopixel_set_rgb(100, 0, 0); //red = fail so send back to flash
            printf("ERROR: Central unavailable. Halting.\n");
            
            // Final error mode — stay here forever
            while (true) {
                neopixel_set_rgb(100, 0, 0);
                tight_loop_contents();
            }
        }

        // Normal case: Send to central good
        printf("Capture %lu done, wrote %u words to central. Sleeping 1 s.\n",
            (unsigned long)(g_capture_index - 1),
            buf_size_words);
        sleep_ms(1000);
    }
}
