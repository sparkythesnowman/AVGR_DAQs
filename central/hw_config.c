/**
 * no-OS-FatFS-SD-SPI-RPi-Pico hardware configuration
 * SD card on SPI1: MISO=GPIO8, MOSI=GPIO15, SCK=GPIO14, CS=GPIO13
 */

#include "hw_config.h"

static spi_t spis[] = {
    {
        .hw_inst   = spi1,
        .miso_gpio = 12,            //
        .mosi_gpio = 15,            //
        .sck_gpio  = 14,
        .baud_rate = 12500 * 1000,   //baud rate 12.5 MHz
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName           = "0:",
        .spi              = &spis[0],
        .ss_gpio          = 13,
        .use_card_detect  = false,
        .card_detect_gpio = 0,
        .card_detected_true = 0,
    }
};

size_t sd_get_num() { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) return &sd_cards[num];
    return NULL;
}

size_t spi_get_num() { return count_of(spis); }
spi_t *spi_get_by_num(size_t num) {
    if (num < spi_get_num()) return &spis[num];
    return NULL;
}