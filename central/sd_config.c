/**
 * SD card mount/unmount helpers using no-OS-FatFS.
 */

#include "sd_config.h"
#include <stdio.h>
#include "f_util.h"
#include "ff.h"
#include "sd_card.h"

static FATFS g_fs;

bool sd_init_driver_and_mount(void) {
    sd_init_driver();

    FRESULT fr = f_mount(&g_fs, "0:", 1);
    if (fr != FR_OK) {
        printf("ERROR: f_mount failed (%s)\n", FRESULT_str(fr));
        return false;
    }
    printf("SD card mounted OK\n");
    return true;
}

void sd_unmount(void) {
    f_unmount("0:");
}