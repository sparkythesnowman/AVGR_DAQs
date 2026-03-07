/**
 * SD card config and capture write
 *
 * - config.txt on SD root: SAMPLE_HZ=..., PREFIX=...
 * - Run dirs: RUN_00000, RUN_00001, ... with DAQ0/, DAQ1/ subdirs
 * - capture_to_sd(): write capture to SD or fall back to flash_log on failure
 * - Flash-stored config: survives SD removal; synced when config.txt changes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "sd_config.h"
#include "flash_log.h"
#include "neopixel.h"
#include "ff.h"

/* Provided by FatFs_SPI library (sd_driver/sd_card.c) */
bool sd_init_driver(void);

/* ---------- Config constants ---------- */
#define DEFAULT_SAMPLE_HZ   (100u*1000000u)
#define DEFAULT_FILE_PREFIX  "CAP_"
#define FILE_PREFIX_MAX_LEN 32
#define RUN_DIR_NAME_MAX_LEN  16
#define CONFIG_PATH        "0:/config.txt"

#define FLASH_CONFIG_SECTOR_SIZE  FLASH_SECTOR_SIZE
#define FLASH_CONFIG_ADDR         (PICO_FLASH_SIZE_BYTES - LOG_REGION_SIZE - FLASH_CONFIG_SECTOR_SIZE)
#define FLASH_CONFIG_MAGIC        0x43464731u   // 'CFG1'
#define FLASH_CONFIG_VERSION      1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t sample_hz;
    char     prefix[FILE_PREFIX_MAX_LEN];
} flash_config_t;

// ---------- SD / FatFS globals ----------
static FATFS   g_fs;
static bool    g_sd_mounted = false;
static bool    g_sd_tried   = false;
static uint32_t g_sd_capture_index = 0;

// ---------- Run directory tracking ----------
static uint32_t g_run_index = 0;
static char     g_run_dir_name[RUN_DIR_NAME_MAX_LEN] = {0};
static uint32_t g_target_sample_hz = DEFAULT_SAMPLE_HZ;
static char     g_file_prefix[FILE_PREFIX_MAX_LEN] = DEFAULT_FILE_PREFIX;

/* ---------- Helpers ----------
 * strip trailing \\r\\n and whitespace from config line */
static void trim_line(char *s) {
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}

/* Read SAMPLE_HZ + PREFIX from dedicated flash sector (below LOG_REGION). */
static bool read_config_from_flash(flash_config_t *out) {
    const flash_config_t *stored =
        (const flash_config_t *)(XIP_BASE + FLASH_CONFIG_ADDR);

    if (stored->magic   != FLASH_CONFIG_MAGIC ||
        stored->version != FLASH_CONFIG_VERSION) {
        return false;
    }

    *out = *stored;
    return true;
}

/* Store config to flash so it survives SD removal. Erase sector, then program. */
static void write_config_to_flash(const flash_config_t *cfg) {
    uint8_t page[FLASH_CONFIG_SECTOR_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, cfg, sizeof(*cfg));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_ADDR, FLASH_CONFIG_SECTOR_SIZE);
    flash_range_program(FLASH_CONFIG_ADDR, page, FLASH_CONFIG_SECTOR_SIZE);
    restore_interrupts(ints);
}

/* Write RUN_INDEX, SAMPLE_HZ, PREFIX, DATA_DEPTH to RUN_XXXXX/meta.txt */
static void write_run_metadata_file(void) {
    if (!g_sd_mounted || g_run_dir_name[0] == '\0') {
        return;
    }

    FIL fil;
    char path[64];
    FRESULT fr;

    snprintf(path, sizeof(path), "%s/meta.txt", g_run_dir_name);

    fr = f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("Meta: f_open('%s') failed (FR=%d)\n", path, fr);
        return;
    }

    char line[64];

    snprintf(line, sizeof(line), "RUN_INDEX=%lu\n", (unsigned long)g_run_index);
    f_puts(line, &fil);

    snprintf(line, sizeof(line), "SAMPLE_HZ=%u\n", g_target_sample_hz);
    f_puts(line, &fil);

    snprintf(line, sizeof(line), "PREFIX=%s\n", g_file_prefix);
    f_puts(line, &fil);

    snprintf(line, sizeof(line), "DATA_DEPTH=%u\n", CAPTURE_N_SAMPLES);
    f_puts(line, &fil);

    f_close(&fil);
    printf("Meta: wrote %s\n", path);
}

/* ---------- Public API ---------- */

/* One-time SD init: call sd_init_driver, mount FatFS. LED: orange blink during init,
 * blue if mount fails (flash fallback). */
void sd_init_if_needed(void) {
    if (g_sd_tried) return;
    g_sd_tried = true;

    printf("SD: initializing...\n");

    neopixel_blink_multiple_start(50, 10, 0, 100, 100, 1000000);

    if (!sd_init_driver()) {
        printf("SD: sd_init_driver() failed; using FLASH fallback.\n");
        neopixel_set_rgb(0, 0, 100);
        g_sd_mounted = false;
        return;
    }

    FRESULT fr = f_mount(&g_fs, "0:", 1);
    if (fr == FR_OK) {
        g_sd_mounted = true;
        printf("SD: mounted OK.\n");
        neopixel_off();
    } else {
        printf("SD: f_mount failed, FR = %d; using FLASH fallback.\n", fr);
        g_sd_mounted = false;
        neopixel_set_rgb(0, 0, 100);
    }
}

/* Read config.txt; reconcile with flash-stored config.
 * LED: blue=no SD, orange=no config file, yellow=new/changed, green=match. */
config_state_t load_config_from_sd(void) {
    sd_init_if_needed();

    if (!g_sd_mounted) {
        printf("Config: SD not mounted; using defaults / flash.\n");
        neopixel_set_rgb(0, 0, 100);
        return CONFIG_STATE_NO_SD;
    }

    FIL fil;
    FRESULT fr = f_open(&fil, CONFIG_PATH, FA_READ);

    if (fr == FR_NO_FILE) {
        printf("Config: %s not found on SD (FR=%d).\n", CONFIG_PATH, fr);

        flash_config_t flash_cfg;
        if (read_config_from_flash(&flash_cfg)) {
            g_target_sample_hz = flash_cfg.sample_hz;
            strncpy(g_file_prefix, flash_cfg.prefix, FILE_PREFIX_MAX_LEN - 1);
            g_file_prefix[FILE_PREFIX_MAX_LEN - 1] = '\0';
            printf("Config: using flash config SAMPLE_HZ=%u PREFIX=\"%s\".\n",
                   g_target_sample_hz, g_file_prefix);
        } else {
            g_target_sample_hz = DEFAULT_SAMPLE_HZ;
            strncpy(g_file_prefix, "CAP_", FILE_PREFIX_MAX_LEN - 1);
            g_file_prefix[FILE_PREFIX_MAX_LEN - 1] = '\0';
            printf("Config: no flash config; using defaults SAMPLE_HZ=%u PREFIX=\"%s\".\n",
                   g_target_sample_hz, g_file_prefix);
        }

        neopixel_set_rgb(100, 35, 0);
        return CONFIG_STATE_NO_FILE;
    } else if (fr != FR_OK) {
        printf("Config: could not open %s (FR=%d); treating as SD failure.\n", CONFIG_PATH, fr);
        neopixel_set_rgb(0, 0, 100);
        return CONFIG_STATE_NO_SD;
    }

    uint32_t sd_sample_hz = DEFAULT_SAMPLE_HZ;
    char     sd_prefix[FILE_PREFIX_MAX_LEN];
    memcpy(sd_prefix, g_file_prefix, sizeof(sd_prefix));

    char line[64];
    while (f_gets(line, sizeof(line), &fil)) {
        trim_line(line);

        if (strncmp(line, "SAMPLE_HZ=", 10) == 0) {
            const char *val = line + 10;
            char *endptr = NULL;
            uint32_t hz = (uint32_t)strtoul(val, &endptr, 10);
            if (hz > 0 && hz <= 100000000u) {
                sd_sample_hz = hz;
                printf("Config: SAMPLE_HZ=%u\n", sd_sample_hz);
            } else {
                printf("Config: SAMPLE_HZ out of range, using default %u\n",
                       DEFAULT_SAMPLE_HZ);
            }
        } else if (strncmp(line, "PREFIX=", 7) == 0) {
            const char *val = line + 7;
            size_t len = strlen(val);
            if (len >= FILE_PREFIX_MAX_LEN) len = FILE_PREFIX_MAX_LEN - 1;
            memcpy(sd_prefix, val, len);
            sd_prefix[len] = '\0';
            printf("Config: PREFIX=\"%s\"\n", sd_prefix);
        }
    }

    f_close(&fil);

    g_target_sample_hz = sd_sample_hz;
    memcpy(g_file_prefix, sd_prefix, sizeof(g_file_prefix));

    flash_config_t flash_cfg;
    bool have_flash_cfg = read_config_from_flash(&flash_cfg);

    flash_config_t sd_cfg = {
        .magic     = FLASH_CONFIG_MAGIC,
        .version   = FLASH_CONFIG_VERSION,
        .sample_hz = sd_sample_hz,
    };
    memset(sd_cfg.prefix, 0, sizeof(sd_cfg.prefix));
    strncpy(sd_cfg.prefix, sd_prefix, sizeof(sd_cfg.prefix) - 1);

    bool same = false;
    if (have_flash_cfg) {
        same = (flash_cfg.sample_hz == sd_cfg.sample_hz) &&
               (strncmp(flash_cfg.prefix, sd_cfg.prefix, FILE_PREFIX_MAX_LEN) == 0);
    }

    if (!have_flash_cfg || !same) {
        write_config_to_flash(&sd_cfg);
        printf("Config: updated flash config.\n");
        neopixel_set_rgb(100, 80, 5);
        return CONFIG_STATE_NEW_OR_CHANGED;
    } else {
        printf("Config: matches flash.\n");
        neopixel_set_rgb(0, 100, 0);
        return CONFIG_STATE_MATCH;
    }
}

/* Scan root for RUN_XXXXX dirs, pick next index, create RUN_XXXXX and DAQ0..DAQn.
 * Sets g_sd_capture_index=0 for this run. Falls back to empty run_dir if mkdir fails. */
void init_run_directory(uint32_t periph_count) {
    if (!g_sd_mounted) {
        printf("RunDir: SD not mounted; no run directory.\n");
        g_run_dir_name[0] = '\0';
        g_run_index = 0;
        g_sd_capture_index = 0;
        return;
    }

    DIR dir;
    FILINFO fno;
    FRESULT fr;

    fr = f_opendir(&dir, "0:");   /* Open root of logical drive 0 (SD) */
    if (fr != FR_OK) {
        printf("RunDir: f_opendir failed (FR=%d)\n", fr);
        g_run_dir_name[0] = '\0';
        g_run_index = 0;
        g_sd_capture_index = 0;
        return;
    }

    /* Scan for existing RUN_00000, RUN_00001, ... to compute next index */
    uint32_t max_run_idx = 0;
    bool found_any = false;
    char digits[6];

    const char *RUN_PREFIX = "RUN_";
    size_t rplen = strlen(RUN_PREFIX);

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }

        if (!(fno.fattrib & AM_DIR)) continue;

        const char *name = fno.fname;
        size_t nlen = strlen(name);
        if (nlen < rplen + 5) continue;

        if (strncmp(name, RUN_PREFIX, rplen) != 0) continue;

        const char *p_digits = name + rplen;
        memcpy(digits, p_digits, 5);
        digits[5] = '\0';

        char *endptr = NULL;
        long idx = strtol(digits, &endptr, 10);
        if (*endptr != '\0' || idx < 0) {
            continue;
        }

        if (!found_any || (uint32_t)idx > max_run_idx) {
            max_run_idx = (uint32_t)idx;
            found_any = true;
        }
    }

    f_closedir(&dir);

    if (found_any) {
        g_run_index = max_run_idx + 1;
    } else {
        g_run_index = 0;
    }

    snprintf(g_run_dir_name, sizeof(g_run_dir_name),
             "RUN_%05lu", (unsigned long)g_run_index);

    printf("RunDir: creating %s\n", g_run_dir_name);

    fr = f_mkdir(g_run_dir_name);
    if (fr != FR_OK && fr != FR_EXIST) {
        printf("RunDir: f_mkdir('%s') failed (FR=%d)\n", g_run_dir_name, fr);
        g_run_dir_name[0] = '\0';
    }

    for (uint32_t i = 0; i < periph_count; i++) {
        char subdir[32];
        snprintf(subdir, sizeof(subdir), "%s/DAQ%lu", g_run_dir_name, (unsigned long)i);
        f_mkdir(subdir);
    }

    g_sd_capture_index = 0;

    if (g_run_dir_name[0]) {
        write_run_metadata_file();
    }
}

/* When no run dir: scan root for PREFIX00000.bin, PREFIX00001.bin, ...
 * Set g_sd_capture_index = max_found + 1 to avoid overwriting. */
void init_sd_capture_index_for_flat_mode(void) {
    if (!g_sd_mounted) {
        printf("Index: SD not mounted; starting at index 0.\n");
        g_sd_capture_index = 0;
        return;
    }

    DIR dir;
    FILINFO fno;
    FRESULT fr;

    fr = f_opendir(&dir, "0:");
    if (fr != FR_OK) {
        printf("Index: f_opendir failed (FR=%d); starting at index 0.\n", fr);
        g_sd_capture_index = 0;
        return;
    }

    uint32_t max_idx = 0;
    bool found_any = false;
    char digits[6];

    size_t plen = strlen(g_file_prefix);

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }

        const char *name = fno.fname;
        if (!name[0]) continue;

        size_t nlen = strlen(name);
        if (nlen < plen + 5 + 4) continue;
        if (strncmp(name, g_file_prefix, plen) != 0) continue;

        const char *suffix = name + nlen - 4;
        if (strcasecmp(suffix, ".bin") != 0) continue;

        const char *p_digits = name + plen;
        if (p_digits + 5 > suffix) continue;

        memcpy(digits, p_digits, 5);
        digits[5] = '\0';

        char *endptr = NULL;
        long idx = strtol(digits, &endptr, 10);
        if (*endptr != '\0' || idx < 0) {
            continue;
        }

        if (!found_any || (uint32_t)idx > max_idx) {
            max_idx = (uint32_t)idx;
            found_any = true;
        }
    }

    f_closedir(&dir);

    if (found_any) {
        g_sd_capture_index = max_idx + 1;
    } else {
        g_sd_capture_index = 0;
    }

    printf("Index: starting capture index %lu (prefix \"%s\")\n",
           (unsigned long)g_sd_capture_index, g_file_prefix);
}

/* Compare config.txt PREFIX with latest RUN_XXXXX/meta.txt PREFIX.
 * If mismatch: print warning, blink blue 3x. */
void check_prefix_against_latest_run(void) {
    if (!g_sd_mounted) return;

    DIR dir;
    FILINFO fno;
    FRESULT fr;

    fr = f_opendir(&dir, "0:");
    if (fr != FR_OK) {
        printf("PrefixCheck: f_opendir failed (FR=%d)\n", fr);
        return;
    }

    const char *RUN_PREFIX = "RUN_";
    size_t rplen = strlen(RUN_PREFIX);

    uint32_t max_run_idx = 0;
    bool found_any = false;
    char digits[6];

    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;

        if (!(fno.fattrib & AM_DIR)) continue;

        const char *name = fno.fname;
        size_t nlen = strlen(name);
        if (nlen < rplen + 5) continue;

        if (strncmp(name, RUN_PREFIX, rplen) != 0) continue;

        memcpy(digits, name + rplen, 5);
        digits[5] = '\0';

        char *endptr;
        long idx = strtol(digits, &endptr, 10);
        if (*endptr != '\0' || idx < 0) continue;

        if (!found_any || (uint32_t)idx > max_run_idx) {
            max_run_idx = (uint32_t)idx;
            found_any = true;
        }
    }

    f_closedir(&dir);

    if (!found_any) {
        printf("PrefixCheck: no existing RUN directories found.\n");
        return;
    }

    char latest_run_dir[32];
    snprintf(latest_run_dir, sizeof(latest_run_dir),
             "RUN_%05lu", (unsigned long)max_run_idx);

    char meta_path[64];
    snprintf(meta_path, sizeof(meta_path),
             "%s/meta.txt", latest_run_dir);

    FIL meta;
    fr = f_open(&meta, meta_path, FA_READ);
    if (fr != FR_OK) {
        printf("PrefixCheck: latest run %s has no meta.txt (FR=%d)\n",
               latest_run_dir, fr);
        return;
    }

    char line[64];
    bool mismatch = false;

    while (f_gets(line, sizeof(line), &meta)) {
        trim_line(line);
        if (strncmp(line, "PREFIX=", 7) == 0) {
            const char *val = line + 7;
            if (strncmp(val, g_file_prefix, FILE_PREFIX_MAX_LEN) != 0) {
                mismatch = true;
            }
            break;
        }
    }

    f_close(&meta);

    if (mismatch) {
        printf("WARNING: prefix mismatch with latest run (%s)\n", latest_run_dir);
        printf(" config.txt PREFIX=\"%s\"\n", g_file_prefix);
        printf(" but latest meta.txt uses different prefix.\n");

        for (int i = 0; i < 3; i++) {
            neopixel_blink_once(0, 0, 255, 150);
            sleep_ms(150);
        }
    }
}

/* Write capture to SD (RUN_XXX/DAQn/PREFIX00000.bin or DAQn/PREFIX00000.bin if flat).
 * On any SD error: fall back to logic_save_capture_to_flash_raw, return false. */
bool capture_to_sd(const uint32_t *cap_words,
                   uint32_t word_count,
                   uint32_t periph_index,
                   uint32_t sample_hz)
{
    sd_init_if_needed();

    if (!g_sd_mounted) {
        printf("SD fallback: SD not mounted. Writing capture to FLASH instead.\n");

        logic_save_capture_to_flash_raw(cap_words,
                                        CAPTURE_N_SAMPLES,
                                        word_count,
                                        0,
                                        sample_hz);
        return false;
    }

    FIL fil;
    FRESULT fr;
    UINT bw;
    char filename[64];

    if (g_run_dir_name[0]) {
        snprintf(filename, sizeof(filename), "%s/DAQ%u/%s%05lu.bin",
            g_run_dir_name,
            periph_index,
            g_file_prefix,
            (unsigned long)g_sd_capture_index++);
    } else {
        snprintf(filename, sizeof(filename), "DAQ%u/%s%05lu.bin",
                periph_index,
                g_file_prefix,
                (unsigned long)g_sd_capture_index++);
    }

    fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        printf("SD fallback: f_open('%s') failed, FR = %d. Writing to FLASH instead.\n",
               filename, fr);

        logic_save_capture_to_flash_raw(cap_words,
                                        CAPTURE_N_SAMPLES,
                                        word_count,
                                        0,
                                        sample_hz);
        return false;
    }

    uint32_t data_bytes = word_count * sizeof(uint32_t);
    fr = f_write(&fil, cap_words, data_bytes, &bw);
    if (fr != FR_OK || bw != data_bytes) {
        printf("SD fallback: f_write failed, FR = %d, bw = %u/%lu. Writing to FLASH instead.\n",
               fr, bw, (unsigned long)data_bytes);
        f_close(&fil);

        logic_save_capture_to_flash_raw(cap_words,
                                        CAPTURE_N_SAMPLES,
                                        word_count,
                                        0,
                                        sample_hz);
        return false;
    }

    fr = f_close(&fil);
    if (fr != FR_OK) {
        printf("SD: f_close('%s') failed, FR = %d; capture likely on SD, but signaling warning.\n",
               filename, fr);
        return true;
    }

    printf("SD: wrote %lu bytes to %s (fsamp ~ %u Hz)\n",
           (unsigned long)data_bytes, filename, sample_hz);
    return true;
}

uint32_t sd_get_target_sample_hz(void) {
    return g_target_sample_hz;
}

const char *sd_get_file_prefix(void) {
    return g_file_prefix;
}

const char *sd_get_run_dir_name(void) {
    return g_run_dir_name;
}

uint32_t sd_get_capture_index(void) {
    return g_sd_capture_index;
}
