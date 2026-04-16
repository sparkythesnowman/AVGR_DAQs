/**
 * PIO logic analyser with ADC clock (sideset) + SPI transfer to central DAQ
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
#include "hardware/gpio.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"

#include "spi_slave_config.h"
#include "neopixel.h"
#include "capture.h"

// ---------- Config ----------
const uint TRIGGER_PIN   = 11;
const uint ARM_PIN       = 29;
const bool TRIGGER_LEVEL = true;

static uint32_t g_capture_index    = 0;
static uint32_t g_target_sample_hz = DEFAULT_SAMPLE_HZ;

// ---------- SPI transfer (master-triggered protocol) ----------
// Called after a capture is complete.
// Flow:
//   1. Wait for master's ACK trigger (ACK_PIN HIGH) — central says "send now"
//   2. Init SPI1 hardware
//   3. Small delay to let master reach spi_read_blocking
//   4. Stream full capture buffer to central
//   5. Deinit SPI1
static bool write_capture_to_spi(const uint32_t *cap_words,
                                  uint32_t word_count,
                                  uint32_t sample_hz)
{
    (void)sample_hz; // reserved for future header use
    
    // 1. Block until master asserts its ACK trigger for this peripheral
    printf("Waiting for master ACK trigger...\n");
    while (!gpio_get(ACK_PIN)) tight_loop_contents();
    printf("ACK received — initialising SPI and sending\n");

    // 2. Init SPI now that master is ready
    spi_slave_init();

    // 3. Small delay: give master time to reach spi_read_blocking
    sleep_ms(50);

    // 4. Send full capture buffer
    spi_central_send(cap_words, word_count * sizeof(uint32_t));

    // 5. Release SPI hardware until next capture
    spi_slave_deinit();

    return true;
}

static void test_gpio_init(void) {
    gpio_init(SPI_RX_PIN);
    gpio_set_dir(SPI_RX_PIN, GPIO_IN);
    gpio_put(SPI_RX_PIN, 0);
    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_IN);
    gpio_put(SPI_CS_PIN, 0);
    gpio_init(SPI_TX_PIN);
    gpio_set_dir(SPI_TX_PIN, GPIO_IN);
    gpio_put(SPI_TX_PIN, 0);
    gpio_init(SPI_SCK_PIN);
    gpio_set_dir(SPI_SCK_PIN, GPIO_IN);
    gpio_put(SPI_SCK_PIN, 0);
}

// ---------- Main ----------
int main(void) {

    stdio_init_all();
    for (int i = 3; i > 0; i--) {
        sleep_ms(1000);
        if (stdio_usb_connected())
            printf("USB connected, starting in %d...\n", i);
    }
    printf("\n=== PIO Logic Analyser (peripheral) ===\n");
    printf("sys_clk = %u Hz\n", clock_get_hz(clk_sys));

    // NeoPixel on GPIO16 via PIO1
    neopixel_init(); // dim orange blink = starting

    // initialize ACK pin
    gpio_init(ACK_PIN);
    gpio_set_dir(ACK_PIN, GPIO_IN);
    gpio_pull_down(ACK_PIN);


    // ---------- PIN TOGGLE TEST ----------
    // {
    //     printf("\n--- PIN TOGGLE TEST ---\n");
    //     test_gpio_init();
    //     uint32_t elapsed_ms = 0;
    //     while (elapsed_ms < 18000) {
    //         printf("\rPERIPHERAL  TX=%d  CS=%d  RX=%d  SCK=%d   ",
    //             gpio_get(SPI_TX_PIN),
    //             gpio_get(SPI_CS_PIN),
    //             gpio_get(SPI_RX_PIN),
    //             gpio_get(SPI_SCK_PIN));
    //         fflush(stdout);
    //         sleep_ms(100);
    //         elapsed_ms += 100;
    //     }
    //     printf("\n");
    // }

    // ---------- ACK PIN TOGGLE TEST ----------
    // printf("\n--- PIN TOGGLE TEST ---\n");
    // {
    //     uint32_t elapsed_ms = 0;
    //     while (elapsed_ms < 18000) {
    //         printf("\rPERIPHERAL  ACK (GPIO%i): %i  ",
    //             ACK_PIN,
    //             gpio_get(ACK_PIN)
    //         );
    //         fflush(stdout);
    //         sleep_ms(1000);
    //         elapsed_ms += 1000;
    //     }
    //     printf("\n");
    // }
    
    // ---------- SPI DIAGNOSTIC ----------
    {
        printf("\n--- SPI diagnostic ---\n");
        // spi_slave_init();
        printf("Waiting for central to be ready...\n");
        stdio_flush();
        uint8_t tx[256] = { 0 };
        for (int i = 0; i < 256; i++) tx[i] = (uint8_t)i;
        uint8_t rx[256] = { 0 };
        printf("Sending 256 bytes (0x00..0xFF)...\n");
        
        while(!gpio_get(ACK_PIN)) tight_loop_contents();
        
        spi_slave_init();
        
        spi_write_read_blocking(SPI_SLAVE_INST, tx, rx, 256);
        printf("Diagnostic TX done\n");

        stdio_flush();

        spi_slave_deinit();
        // Halt here until further instructions on the capture loop
        while (true) tight_loop_contents();
    }
    // ---------- CAPTURE SETUP (below diagnostic halt) ----------

    neopixel_cancel_timer();

    printf("PIO DAQ: Initialising\n");
    set_sys_clock_khz(200000, true);
    printf("Config: SAMPLE_HZ=%u\n", g_target_sample_hz);

    uint32_t buf_size_words = words_for_samples(CAPTURE_N_SAMPLES, CAPTURE_PIN_COUNT);
    uint32_t *capture_buf   = (uint32_t *)malloc(buf_size_words * sizeof(uint32_t));
    hard_assert(capture_buf);

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS |
                            BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    PIO  pio = pio0;
    uint sm  = 0;

    uint32_t f_sys  = clock_get_hz(clk_sys);
    float clkdiv    = (float)f_sys / (2.0f * (float)g_target_sample_hz);

    uint prog_off = clk_sample_program_load(pio);
    clk_sample_sm_init(pio, sm, prog_off, ADC_CLK_PIN, CAPTURE_PIN_BASE, clkdiv);

    gpio_init(TRIGGER_PIN);
    gpio_set_dir(TRIGGER_PIN, GPIO_IN);
    gpio_pull_down(TRIGGER_PIN);

    gpio_init(ARM_PIN);
    gpio_set_dir(ARM_PIN, GPIO_IN);
    gpio_pull_down(ARM_PIN);

    gpio_init(ACK_PIN);
    gpio_set_dir(ACK_PIN, GPIO_IN);
    gpio_pull_down(ACK_PIN);

    uint dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));

    uint32_t sample_hz = (uint32_t)((float)f_sys / (2.0f * clkdiv) + 0.5f);
    printf("Configured sample rate ~%u Hz (clkdiv=%.3f)\n", sample_hz, clkdiv);

    // ---------- Capture loop ----------
    while (true) {

        dma_channel_configure(dma_chan, &c,
            capture_buf,
            &pio->rxf[sm],
            buf_size_words,
            false
        );

        pio_sm_set_enabled(pio, sm, false);
        pio_sm_clear_fifos(pio, sm);
        pio_sm_restart(pio, sm);
        pio_sm_exec(pio, sm, pio_encode_wait_gpio(TRIGGER_LEVEL, TRIGGER_PIN));

        while (!gpio_get(ARM_PIN)) tight_loop_contents();
        neopixel_blink_once(50, 50, 50, 500); // grey = armed

        printf("Arming trigger for capture %lu...\n", (unsigned long)g_capture_index);
        g_capture_index++;

        dma_channel_start(dma_chan);
        pio_sm_set_enabled(pio, sm, true);
        dma_channel_wait_for_finish_blocking(dma_chan);
        pio_sm_set_enabled(pio, sm, false);

        // Transfer to central via ACK/READY protocol (SPI init deferred to inside)
        bool ok = write_capture_to_spi(capture_buf, buf_size_words, sample_hz);
        sleep_ms(3000);

        if (ok) {
            neopixel_set_rgb(0, 100, 0); // green = sent
        } else {
            printf("ERROR: transfer to central failed. Halting.\n");
            while (true) {
                neopixel_set_rgb(100, 0, 0);
                tight_loop_contents();
            }
        }

        printf("Capture %lu done, sent %u words to central.\n",
               (unsigned long)(g_capture_index - 1), buf_size_words);
        sleep_ms(1000);
    }
}