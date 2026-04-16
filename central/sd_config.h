#pragma once

#include <stdbool.h>

/* Mount SD card ("0:"). Returns true on success. */
bool sd_init_driver_and_mount(void);

/* Unmount SD card. */
void sd_unmount(void);