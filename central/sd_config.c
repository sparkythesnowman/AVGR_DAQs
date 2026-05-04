/**
 * SD card configuration + capture logging.
 * SPI1 mapping: SCK=GP10, TX/MOSI=GP11, RX/MISO=GP12, CS=GP13
 */

#include "sd_config.h"

#include <stdio.h>

#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "neopixel.h"
#include "sd_card.h"

// ---- FatFs SPI hardware config (merged from hw_config.c) ----
static spi_t spis[] = {
    {
        .hw_inst = spi1,
        .miso_gpio = 8,             // RX (MISO)
        .mosi_gpio = 11,             // TX (MOSI)
        .sck_gpio = 10,
        .baud_rate = 10000 * 1000,   // 10 MHz
        
        .set_drive_strength = false,
        .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .sck_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_4MA,
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",     // Logical drive name for FatFS
        .spi    = &spis[0], // SPI used by this card

        .ss_gpio = 13,      // SD card CS pin (GPIO number)

        // We’re *not* using a card-detect switch
        .use_card_detect   = false,
        .card_detect_gpio  = 0,   // ignored when use_card_detect == false
        .card_detected_true = 0,  // ignored when use_card_detect == false

        .set_drive_strength       = false,
        .ss_gpio_drive_strength   = GPIO_DRIVE_STRENGTH_4MA,
    }
};

size_t sd_get_num(void) {
    return sizeof(sd_cards) / sizeof(sd_cards[0]);
}

sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) {
        return &sd_cards[num];
    }
    return NULL;
}

size_t spi_get_num(void) {
    return sizeof(spis) / sizeof(spis[0]);
}

spi_t *spi_get_by_num(size_t num) {
    if (num < spi_get_num()) {
        return &spis[num];
    }
    return NULL;
}

// ---- SD capture write API ----
static FATFS g_fs;
static bool g_mounted = false;
static bool g_init_tried = false;
static uint32_t g_file_index = 1;

bool sd_init(void) {
    if (g_mounted) {
        return true;
    }
    // if (g_init_tried) {
    //     return false;
    // }

    // g_init_tried = true;

    if (!sd_init_driver()) {
        printf("SD: sd_init_driver() failed.\n");
        return false;
    }

    FRESULT fr = f_mount(&g_fs, "0:", 1);
    if (fr != FR_OK) {
        printf("SD: f_mount failed (%s)\n", FRESULT_str(fr));
        return false;
    }

    g_mounted = true;
    printf("SD: mounted OK.\n");
    return true;
}

bool write_capture_to_sd(const uint8_t *buf, uint32_t len) {
    if (!buf || len == 0) {
        printf("SD: invalid write buffer.\n");
        neopixel_set_rgb(100, 0, 0); //red
        return false;
    }

    if (!sd_init()) {
        printf("SD: sd_init() failed.\n");
        neopixel_set_rgb(100, 0, 0); //red
        return false;
    }

    neopixel_set_rgb(0, 0, 100);  // blue while writing
    
    char filename[32];
    snprintf(filename, sizeof(filename), "0:cap_%04lu.bin",
             (unsigned long)g_file_index);
    printf("SD: writing to file %s\n", filename);

    FIL fil;
    UINT written = 0;

    FRESULT fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
    printf("SD: f_open(%s) result: %s\n", filename, FRESULT_str(fr));
    if (fr != FR_OK) {
        printf("SD: f_open(%s) failed (%s)\n", filename, FRESULT_str(fr));
        neopixel_set_rgb(100, 0, 0);
        return false;
    }

    fr = f_write(&fil, buf, len, &written);
    printf("SD: f_write(%s) wrote %lu bytes\n", filename, written);
    if (fr != FR_OK || written != len) {
        printf("SD: f_write failed (%u/%lu, %s)\n",
               written, (unsigned long)len, FRESULT_str(fr));
        (void)f_close(&fil);
        neopixel_set_rgb(100, 0, 0);
        return false;
    }

    fr = f_close(&fil);
    if (fr != FR_OK) {
        printf("SD: f_close failed (%s)\n", FRESULT_str(fr));
        neopixel_set_rgb(100, 0, 0);
        return false;
    }

    printf("SD: wrote %lu bytes to %s\n", (unsigned long)len, filename);
    g_file_index++;
    neopixel_set_rgb(0, 100, 0);  // green on success
    return true;
}

void sd_unmount(void) {
    if (!g_mounted) return;
    f_unmount("0:");
    g_mounted = false;
    printf("SD: unmounted.\n");
}
