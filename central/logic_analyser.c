/**
 * Central DAQ — SPI master
 *
 * SPI0 pins (hardware CS):
 *   RX=GPIO0  CS=GPIO1  TX=GPIO3  SCK=GPIO2
 *
 * Peripheral handshake GPIOs:
 *   Periph 0: READY=GPIO26 (out, to periph)  ACK=GPIO27 (in, from periph)
 *   Periph 1: READY=GPIO28 (out, to periph)  ACK=GPIO29 (in, from periph)
 *
 * SD card on SPI1: MISO=GPIO8, MOSI=GPIO15, SCK=GPIO14, CS=GPIO13
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"

#include "neopixel.h"
#include "sd_config.h"
#include "flash_log.h"

// ---------- SPI0 (DAQ comms) ----------
#define DAQ_SPI_INST    spi0
#define SPI_RX_PIN      0
#define SPI_CS_PIN      1
#define SPI_TX_PIN      3
#define SPI_SCK_PIN     6
#define SPI_BAUD_HZ     (12500u * 1000u)   // 12.5 MHz

// ---------- Peripheral table ----------
typedef struct {
    uint ack_gpio;   // central output: HIGH = ready to receive
} periph_t;

static const periph_t periphs[] = {
    { 26 },
    { 28 },
};
#define N_PERIPHS  2

// ---------- Capture buffer (131072 bytes = 128 kB) ----------
#define CAPTURE_BYTES   131072u
static uint8_t rx_buf[CAPTURE_BYTES];

// ---------- SPI helpers ----------
static void spi_master_init(void) {
    spi_init(DAQ_SPI_INST, SPI_BAUD_HZ);
    gpio_set_function(SPI_RX_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_CS_PIN,  GPIO_FUNC_SPI);   // hardware CS
    gpio_set_function(SPI_TX_PIN,  GPIO_FUNC_SPI);
    gpio_set_function(SPI_SCK_PIN, GPIO_FUNC_SPI);
}

// ---------- Periph ACK init ----------
static void periph_ack_init(void) {
    for (size_t i = 0; i < N_PERIPHS; i++) {
        gpio_init(periphs[i].ack_gpio);
        gpio_set_dir(periphs[i].ack_gpio, GPIO_OUT);
        gpio_put(periphs[i].ack_gpio, 0);
    }
}

static void test_gpio_init(void) {
    gpio_init(SPI_RX_PIN);
    gpio_set_dir(SPI_RX_PIN, GPIO_OUT);
    gpio_put(SPI_RX_PIN, 0);
    gpio_init(SPI_CS_PIN);
    gpio_set_dir(SPI_CS_PIN, GPIO_OUT);
    gpio_put(SPI_CS_PIN, 0);
    gpio_init(SPI_TX_PIN);
    gpio_set_dir(SPI_TX_PIN, GPIO_OUT);
    gpio_put(SPI_TX_PIN, 0);
    gpio_init(SPI_SCK_PIN);
    gpio_set_dir(SPI_SCK_PIN, GPIO_OUT);
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
    printf("\n=== Central DAQ ===\n");
    printf("sys_clk = %u Hz\n", clock_get_hz(clk_sys));

    neopixel_init();  // dim orange = starting
    periph_ack_init();

    // ---------- PIN TOGGLE TEST ----------
    // {
    //     test_gpio_init();
    //     printf("\n--- PIN TOGGLE TEST ---\n");
    //     gpio_put(SPI_RX_PIN, 1);
    //     sleep_ms(4000);
    //     printf("RX pin high for 4 seconds\n");
    //     gpio_put(SPI_RX_PIN, 0);
    //     sleep_ms(500);
    //     printf("RX pin low for 500ms\n");
    //     gpio_put(SPI_CS_PIN, 1);
    //     sleep_ms(4000);
    //     printf("CS pin high for 4 seconds\n");
    //     gpio_put(SPI_CS_PIN, 0);
    //     sleep_ms(500);
    //     printf("CS pin low for 500ms\n");
    //     gpio_put(SPI_TX_PIN, 1);
    //     sleep_ms(4000);
    //     printf("TX pin high for 4 seconds\n");
    //     gpio_put(SPI_TX_PIN, 0);
    //     sleep_ms(500);
    //     printf("TX pin low for 500ms\n");
    //     gpio_put(SPI_SCK_PIN, 1);
    //     sleep_ms(4000);
    //     printf("SCK pin high for 4 seconds\n");
    //     gpio_put(SPI_SCK_PIN, 0);
    //     sleep_ms(500);
    //     printf("SCK pin low for 500ms\n");
    // }
    // ---------- ACK PIN TOGGLE TEST ----------
    // {
    //     printf("\n--- ACK PIN TOGGLE TEST ---\n");


    //     gpio_put(periphs[0].ack_gpio, 1);
    //     printf("Asserting ACK pin (GPIO %u) for 5 seconds\n", periphs[0].ack_gpio);
    //     sleep_ms(5000);
    //     gpio_put(periphs[0].ack_gpio, 0);
    //     sleep_ms(500);
    //     gpio_put(periphs[1].ack_gpio, 1);
    //     sleep_ms(4000);
    //     gpio_put(periphs[1].ack_gpio, 0);
    //     sleep_ms(500);
    // }
    // ---------- SPI DIAGNOSTIC ----------
    {
        printf("\n--- SPI diagnostic ---\n");
        spi_master_init();
        neopixel_set_rgb(0, 0, 100); // blue = SPI diagnostic started
        sleep_ms(8000); //setup time
        uint8_t tx[256] = {0};
        uint8_t rx[256] = {0};
        printf("Receiving 256 bytes from peripheral...\n");
        stdio_flush();
        
        for (size_t i = 0; i < N_PERIPHS; i++) {
            printf("Asserting READY pin for peripheral %zu: pin %i \n", i, periphs[i].ack_gpio);
            gpio_put(periphs[i].ack_gpio, 1);
            sleep_us(500);
            spi_write_read_blocking(DAQ_SPI_INST, tx, rx, 256);

            printf("Deasserting READY pin for peripheral %zu: pin %i \n", i, periphs[i].ack_gpio);
            gpio_put(periphs[i].ack_gpio, 0);
            
            for (int j = 0; j < 16; j++) {
                int off = 16 * j;
                printf("RX[%3d:%3d]: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                       off, off + 16,
                       rx[off],    rx[off+1],  rx[off+2],  rx[off+3],
                       rx[off+4],  rx[off+5],  rx[off+6],  rx[off+7],
                       rx[off+8],  rx[off+9],  rx[off+10], rx[off+11],
                       rx[off+12], rx[off+13], rx[off+14], rx[off+15]);
            }

            neopixel_cancel_timer();

            bool diag_ok = true;
            for (int k = 0; k < 256; k++) {
                if (rx[k] != (uint8_t)k) {
                    diag_ok = false;
                    break;
                }
            }
            if (diag_ok) {
                neopixel_blink_once(0, 100, 0, 1000); //green
                printf("Diagnostic PASSED\n");
            } else {
                neopixel_blink_once(100, 0, 0, 1000); //red
                printf("Diagnostic FAILED\n");
            }
       
        }
        stdio_flush();

        // Halt here until capture loop is enabled
        while (true) tight_loop_contents();
    }
        
    // ---------- CAPTURE LOOP ----------

    // Mount SD card for logging
    if (!sd_init_driver_and_mount()) {
        neopixel_set_rgb(100, 0, 0);
        printf("ERROR: SD mount failed. Halting.\n");
        while (true) tight_loop_contents();
    }
    neopixel_set_rgb(0, 0, 50);   // dim blue = SD ready, waiting

    printf("Waiting for peripherals...\n");

    while (true) {
        for (size_t i = 0; i < N_PERIPHS; i++) {

            printf("Periph %zu: asserting ACK trigger\n", i);
            gpio_put(periphs[i].ack_gpio, 1);
            neopixel_set_rgb(50, 50, 0);   // yellow = transfer in progress

            // Give peripheral time to i.it SPI after seeing trigger
            sleep_ms(100);

            // Receive full capture
            spi_read_blocking(DAQ_SPI_INST, 0, rx_buf, CAPTURE_BYTES);

            gpio_put(periphs[i].ack_gpio, 0);
            printf("Periph %zu: transfer done\n", i);

            // Write to SD
            if (flash_log_open_new()) {
                flash_log_write(rx_buf, CAPTURE_BYTES);
                flash_log_close();
                printf("Saved capture %lu\n",
                       (unsigned long)(flash_log_get_index() - 1));
                neopixel_blink_once(0, 100, 0, 200);  // green flash = saved
            } else {
                printf("ERROR: SD write failed\n");
                neopixel_set_rgb(100, 0, 0);
            }
        }
    }
}