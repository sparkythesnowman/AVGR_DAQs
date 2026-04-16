/**
 * SD capture file logging.
 * Each capture is written to a sequentially numbered binary file: cap_NNNN.bin
 */

#include "flash_log.h"
#include <stdio.h>
#include "ff.h"
#include "f_util.h"

static FIL     g_file;
static bool    g_open       = false;
static uint32_t g_file_index = 0;

bool flash_log_open_new(void) {
    if (g_open) flash_log_close();

    char fname[32];
    snprintf(fname, sizeof(fname), "0:cap_%04lu.bin", (unsigned long)g_file_index);

    FRESULT fr = f_open(&g_file, fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("ERROR: f_open(%s) failed (%s)\n", fname, FRESULT_str(fr));
        return false;
    }

    g_open = true;
    printf("Opened %s for writing\n", fname);
    return true;
}

bool flash_log_write(const uint8_t *data, uint32_t len) {
    if (!g_open) return false;

    UINT written;
    FRESULT fr = f_write(&g_file, data, len, &written);
    if (fr != FR_OK || written != len) {
        printf("ERROR: f_write failed (wrote %u/%lu, %s)\n",
               written, (unsigned long)len, FRESULT_str(fr));
        return false;
    }
    return true;
}

void flash_log_close(void) {
    if (!g_open) return;
    f_close(&g_file);
    g_open = false;
    g_file_index++;
}

uint32_t flash_log_get_index(void) {
    return g_file_index;
}