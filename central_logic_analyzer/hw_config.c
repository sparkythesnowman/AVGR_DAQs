/* hw_config.c
   Copyright 2021 Carl John Kugler III
   Licensed under the Apache License, Version 2.0 (the "License");
 *
 * FatFs_SPI board config: SPI bus for SD card, pin assignments.
 * sd_get_num, sd_get_by_num, spi_get_num, spi_get_by_num are required by the library.
 */

#include <string.h>

#include "my_debug.h"
#include "hw_config.h"
#include "ff.h"      /* Obtains integer types */
#include "diskio.h" /* Declarations of disk functions */

/* ---------- SPI configuration (SPI1 for SD card; logic_analyser uses SPI0 for DAQs) ---------- */

static spi_t spis[] = {  /* One SPI instance for SD card */
    {
        .hw_inst  = spi1,   /* SPI1 for SD (SPI0 used by logic_analyser for DAQs) */
        .miso_gpio = 12,    /* GPIO numbers (not physical pin numbers) */
        .mosi_gpio = 15,
        .sck_gpio  = 14,
        .baud_rate = 12500 * 1000,  // 12.5 MHz to start with
        .set_drive_strength = false,
        .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_4MA,
        .sck_gpio_drive_strength  = GPIO_DRIVE_STRENGTH_4MA,
    }
};

/* ---------- SD card configuration ---------- */

static sd_card_t sd_cards[] = {  /* One SD card on SPI1 */
    {
        .pcName = "0:",     /* Logical drive name for FatFS (f_open("0:/...")) */
        .spi    = &spis[0], /* SPI used by this card */

        .ss_gpio = 13,      /* SD card chip select (GPIO number) */

        /* No card-detect switch */
        .use_card_detect   = false,
        .card_detect_gpio  = 0,   /* Ignored when use_card_detect == false */
        .card_detected_true = 0,  /* Ignored when use_card_detect == false */

        .set_drive_strength       = false,
        .ss_gpio_drive_strength   = GPIO_DRIVE_STRENGTH_4MA,
    }
};

/* ---------- Accessors required by FatFs_SPI (diskio.c calls these) ---------- */

size_t sd_get_num(void) {
    return count_of(sd_cards);
}

sd_card_t *sd_get_by_num(size_t num) {
    if (num < sd_get_num()) {    /* Strictly < */
        return &sd_cards[num];
    } else {
        return NULL;
    }
}

size_t spi_get_num(void) {
    return count_of(spis);
}

spi_t *spi_get_by_num(size_t num) {
    if (num < spi_get_num()) {
        return &spis[num];
    } else {
        return NULL;
    }
}
