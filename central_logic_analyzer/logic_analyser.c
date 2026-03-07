/**
 * SPI logic analyser (central DAQ master)
 *
 * Hardware: RP2040 Pico polls peripheral DAQ boards over SPI0.
 * - Probe ACK pins: when low, peripheral has data ready.
 * - Assert CS, read capture via MISO, deassert CS.
 * - Write to SD card; if SD fails, write to on-chip flash as backup.
 *
 * USB serial: when a flash backup exists at boot, wait for 'd' (dump hex)
 * or 'e' (erase) before continuing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"

#include "neopixel.h"
#include "flash_log.h"
#include "sd_config.h"
#include "ff.h"
#include "hw_config.h"

#include "hardware/spi.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/clocks.h"

/* count_of() from pico/platform.h */

/* ---------- Peripheral DAQ table ----------
 * Each peripheral has: CS (chip select, output) and ACK (data-ready, input).
 * SPI0 is shared; only one peripheral drives MISO at a time (CS selects which). */
typedef struct {
    uint cs_gpio;   /* Chip select output (active low) */
    uint ack_gpio;  /* Acknowledge input (low = data ready to read) */
} periph_t;

static const periph_t periphs[] = {
    { .cs_gpio = 2, .ack_gpio = 6},
    { .cs_gpio = 3, .ack_gpio = 7},
};

/* ---------- Helpers ----------
 * SPI captures may arrive bit-reversed; reverse each byte for correct ordering. */
static inline uint8_t reverse_byte(uint8_t x) {
    x = (x >> 4) | (x << 4);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);
    return x;
}

/* ---------- Main ---------- */

int main(void) {
    stdio_init_all();   /* UART + USB serial */

    /* Allow up to 5 s for user to open serial terminal before first printf */
    for (int i = 5; i > 0; i--) {
        sleep_ms(1000);
        if (stdio_usb_connected()) break;
    }
    printf("\n=== Central Logic Analyser ===\n");
    printf("sys_clk = %u Hz\n", clock_get_hz(clk_sys));

    neopixel_init();    /* PIO+GPIO16, timer for non-blocking blink */

    /* --- FLASH RECOVERY MODE ---
     * If a previous capture was written to flash (e.g. SD card writing failed),
     * we pause here and wait for USB serial. User can:
     *   'd' = dump capture as hex over USB (for export/analysis)
     *   'e' = erase the flash region (free it for new captures)
     * Purple LED = waiting in this recovery mode. */
    if (logic_has_saved_capture()) {
        printf("FLASH capture present.\n");
        neopixel_set_rgb(100, 0, 50);   /* Purple: recovery mode active */

        bool was_connected = false;     /* Track USB attach to print prompt once */
        while (true) {
            bool now = stdio_usb_connected();

            /* On first USB connect: short settle delay, then print instructions */
            if (now && !was_connected) {
                sleep_ms(50);           /* Let serial stabilize before first printf */
                printf("READY: saved FLASH capture present.\n");
                printf("Send 'd' to dump, 'e' to erase.\n");
            }
            was_connected = now;

            /* Non-blocking char read: only when USB is connected */
            int c = now ? getchar_timeout_us(0) : PICO_ERROR_TIMEOUT;
            if (c == 'd') {
                logic_dump_saved_capture_hex();   /* Output hex to serial */
            } else if (c == 'e') {
                logic_erase_saved_capture();       /* Erase flash region */
                printf("Erased FLASH capture. Power-cycle or press reset.\n");
                neopixel_off();
                break;                             /* Exit recovery loop, continue to main DAQ */
            }

            tight_loop_contents();     /* Low-power yield when idle */
        }
    }

    /* --- SD init & config --- */
    sd_init_if_needed();                        /* Mount SD if present */
    config_state_t cfg_state = load_config_from_sd();  /* Read config.txt, set LED */
    (void)cfg_state;

    neopixel_cancel_timer();                    /* Stop startup blink; config sets final color */

    printf("PIO DAQ: Initializing\n");
    set_sys_clock_khz(200000, true);            /* 200 MHz for capture timing */

    /* Create RUN_XXXXX dir and per-peripheral subdirs; or fall back to flat layout */
    init_run_directory(count_of(periphs));

    /* If no run dir (SD missing or mkdir failed), scan root for PREFIX*.bin to set file index */
    if (sd_get_run_dir_name()[0] == '\0') {
        init_sd_capture_index_for_flat_mode();
    }

    /* Warn if config.txt PREFIX differs from latest run's meta.txt PREFIX */
    check_prefix_against_latest_run();

    printf("Config: SAMPLE_HZ=%u, PREFIX=\"%s\"\n",
           sd_get_target_sample_hz(), sd_get_file_prefix());
    if (sd_get_run_dir_name()[0]) {
        printf("RunDir: using directory %s\n", sd_get_run_dir_name());
    } else {
        printf("RunDir: no run directory (SD missing or mkdir failed), writing flat.\n");
    }

    /* Capture buffer: CAPTURE_N_SAMPLES bytes (raw byte stream from SPI) */
    uint32_t buf_size_bytes = CAPTURE_N_SAMPLES;
    uint32_t *capture_buf = (uint32_t *)malloc(buf_size_bytes);
    if (!capture_buf) {
        printf("FATAL: malloc failed\n");
        for (;;) tight_loop_contents();
    }

    /* --- SPI0: SCK=0, MOSI=1, MISO=5 (shared by all peripherals) --- */
    spi_init(spi0, 1000000);
    gpio_set_function(0, GPIO_FUNC_SPI);   /* SCK */
    gpio_set_function(1, GPIO_FUNC_SPI);  /* MOSI */
    gpio_set_function(5, GPIO_FUNC_SPI);  /* MISO */

    /* Per-peripheral CS and ACK GPIOs */
    for (int i = 0; i < count_of(periphs); i++) {
        gpio_init(periphs[i].cs_gpio);
        gpio_set_dir(periphs[i].cs_gpio, GPIO_OUT);
        gpio_put(periphs[i].cs_gpio, 1);   /* CS high = deselected when idle */

        gpio_init(periphs[i].ack_gpio);
        gpio_set_dir(periphs[i].ack_gpio, GPIO_IN);
        gpio_pull_up(periphs[i].ack_gpio); /* ACK pulled up; peripheral pulls low when ready */
    }

    uint32_t sample_hz = sd_get_target_sample_hz();
    printf("Configured sample rate ~%u Hz\n", sample_hz);

    /* --- Main capture loop --- */
    while (true) {
        bool any_failure = false;

        for (int i = 0; i < count_of(periphs); i++) {
            if (gpio_get(periphs[i].ack_gpio)) continue;  /* ACK high = no data, skip */

            gpio_put(periphs[i].cs_gpio, 0);              /* Assert CS: select this peripheral */
            neopixel_blink_once(50, 50, 50, 500);         /* Grey blink = reading */
            printf("Capturing from DAQ %d\n", i);

            /* Blocking SPI read: peripheral streams data on MISO */
            spi_read_blocking(spi0, 0x00, (uint8_t *)capture_buf, CAPTURE_N_SAMPLES);

            gpio_put(periphs[i].cs_gpio, 1);              /* Deassert CS */

            /* Bit-reverse each byte (peripheral may send MSB-first; we want correct bit order) */
            for (uint32_t j = 0; j < CAPTURE_N_SAMPLES; j++) {
                ((uint8_t *)capture_buf)[j] = reverse_byte(((uint8_t *)capture_buf)[j]);
            }

            /* Write to SD (or flash if SD fails). Returns false on fallback to flash. */
            bool wrote_to_sd = capture_to_sd(capture_buf, CAPTURE_N_SAMPLES/4, i, sample_hz);
            if (wrote_to_sd) {
                neopixel_blink_once(0, 100, 0, 500);     /* Green blink = SD OK */
            } else {
                any_failure = true;                       /* Fell back to flash */
            }

            printf("Capture %lu done, wrote %u words to SD. Sleeping 1 s.\n",
                   (unsigned long)(sd_get_capture_index() - 1),
                   CAPTURE_N_SAMPLES/4);
        }

        /* After polling all peripherals: green if all OK, red and halt if any fell back to flash */
        if (!any_failure) {
            neopixel_set_rgb(0, 100, 0);                  /* Solid green */
        } else {
            neopixel_set_rgb(100, 0, 0);
            printf("ERROR: SD unavailable. Capture written to FLASH. Halting.\n");
            while (true) {
                neopixel_set_rgb(100, 0, 0);              /* Solid red, wait for reset */
                tight_loop_contents();
            }
        }
        sleep_ms(1000);   /* Pause before next poll cycle */
    }
}
