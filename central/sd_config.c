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
#include "flash_log.h"

// ---- FatFs SPI hardware config (merged from hw_config.c) ----
static spi_t spis[] = {
    {
        .hw_inst = spi1,
        .miso_gpio = 12,             // RX (MISO)
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

// static config_state_t load_config_from_sd(void) {
//     // Ensure SD init was attempted
//     sd_init_if_needed();

//     if (!g_sd_mounted) {
//         printf("Config: SD not mounted; using defaults / flash.\n");
//         // LED blue: will write to flash
//         neopixel_set_rgb(0, 0, 100);
//         g_config_state = CONFIG_STATE_NO_SD;
//         return g_config_state;
//     }

//     FIL fil;
//     FRESULT fr = f_open(&fil, CONFIG_PATH, FA_READ);

//     if (fr == FR_NO_FILE) {
//         // SD is there, but no config.txt -> ORANGE state
//         printf("Config: %s not found on SD (FR=%d).\n", CONFIG_PATH, fr);

//         // Try to fall back to flash config
//         flash_config_t flash_cfg;
//         if (read_config_from_flash(&flash_cfg)) {
//             g_target_sample_hz = flash_cfg.sample_hz;
//             strncpy(g_file_prefix, flash_cfg.prefix, FILE_PREFIX_MAX_LEN - 1);
//             g_file_prefix[FILE_PREFIX_MAX_LEN - 1] = '\0';
//             printf("Config: using flash config SAMPLE_HZ=%u PREFIX=\"%s\".\n",
//                    g_target_sample_hz, g_file_prefix);
//         } else {
//             // No flash config either -> use built-in defaults
//             g_target_sample_hz = DEFAULT_SAMPLE_HZ;
//             strncpy(g_file_prefix, "CAP_", FILE_PREFIX_MAX_LEN - 1);
//             g_file_prefix[FILE_PREFIX_MAX_LEN - 1] = '\0';
//             printf("Config: no flash config; using defaults SAMPLE_HZ=%u PREFIX=\"%s\".\n",
//                    g_target_sample_hz, g_file_prefix);
//         }

//         // Orange LED: SD OK but no config file
//         neopixel_set_rgb(100, 35, 0);
//         g_config_state = CONFIG_STATE_NO_FILE;
//         return g_config_state;
//     } else if (fr != FR_OK) {
//         // Some other SD error while opening config.txt
//         printf("Config: could not open %s (FR=%d); treating as SD failure.\n", CONFIG_PATH, fr);
//         neopixel_set_rgb(0, 0, 100);
//         g_config_state = CONFIG_STATE_NO_SD;
//         return g_config_state;
//     }

//     // --- SD config file is open here ---

//     // Start from defaults
//     uint32_t sd_sample_hz = DEFAULT_SAMPLE_HZ;
//     char     sd_prefix[FILE_PREFIX_MAX_LEN];
//     memcpy(sd_prefix, g_file_prefix, sizeof(sd_prefix));

//     char line[64];
//     while (f_gets(line, sizeof(line), &fil)) {
//         trim_line(line);

//         if (strncmp(line, "SAMPLE_HZ=", 10) == 0) {
//             const char *val = line + 10;
//             char *endptr = NULL;
//             uint32_t hz = (uint32_t)strtoul(val, &endptr, 10);
//             if (hz > 0 && hz <= 100000000u) {   // sanity clamp
//                 sd_sample_hz = hz;
//                 printf("Config: SAMPLE_HZ=%u\n", sd_sample_hz);
//             } else {
//                 printf("Config: SAMPLE_HZ out of range, using default %u\n",
//                        DEFAULT_SAMPLE_HZ);
//             }
//         } else if (strncmp(line, "PREFIX=", 7) == 0) {
//             const char *val = line + 7;
//             size_t len = strlen(val);
//             if (len >= FILE_PREFIX_MAX_LEN) len = FILE_PREFIX_MAX_LEN - 1;
//             memcpy(sd_prefix, val, len);
//             sd_prefix[len] = '\0';
//             printf("Config: PREFIX=\"%s\"\n", sd_prefix);
//         }
//     }

//     f_close(&fil);

//     // Apply SD config to globals
//     g_target_sample_hz = sd_sample_hz;
//     memcpy(g_file_prefix, sd_prefix, sizeof(g_file_prefix));

//     // Now reconcile with flash-stored config
//     flash_config_t flash_cfg;
//     bool have_flash_cfg = read_config_from_flash(&flash_cfg);

//     flash_config_t sd_cfg = {
//         .magic     = FLASH_CONFIG_MAGIC,
//         .version   = FLASH_CONFIG_VERSION,
//         .sample_hz = sd_sample_hz,
//     };
//     memset(sd_cfg.prefix, 0, sizeof(sd_cfg.prefix));
//     strncpy(sd_cfg.prefix, sd_prefix, sizeof(sd_cfg.prefix) - 1);

//     bool same = false;
//     if (have_flash_cfg) {
//         same = (flash_cfg.sample_hz == sd_cfg.sample_hz) &&
//                (strncmp(flash_cfg.prefix, sd_cfg.prefix, FILE_PREFIX_MAX_LEN) == 0);
//     }

//     if (!have_flash_cfg || !same) {
//         // New or changed config: write to flash and show yellow
//         write_config_to_flash(&sd_cfg);
//         printf("Config: updated flash config.\n");
//         // Yellow: new or changed config
//         neopixel_set_rgb(100, 80, 5);
//         g_config_state = CONFIG_STATE_NEW_OR_CHANGED;
//     } else {
//         // Match: green (SD present, config OK)
//         printf("Config: matches flash.\n");
//         neopixel_set_rgb(0, 100, 0);
//         g_config_state = CONFIG_STATE_MATCH;
//     }

//     return g_config_state;
// }

bool write_capture_to_sd(const uint8_t *buf, uint32_t len, const trigger_time_t *trigger_time, uint8_t daq_num) {
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
    // STORE TO FLASH****
    neopixel_set_rgb(0, 0, 100);  // blue while writing
    
    char filename[32];
    // changed capture nomenclature
    if (trigger_time) {
        snprintf(filename, sizeof(filename), "0:cap_%02u_%02u_%02u_%02u_%02u.bin",
                 trigger_time->day,
                 trigger_time->hour,
                 trigger_time->min,
                 trigger_time->sec, daq_num);
    } else {
        snprintf(filename, sizeof(filename), "0:cap_%04lu.bin",
                 (unsigned long)g_file_index);
    }
    printf("SD: writing to file %s\n", filename);

    FIL fil;
    UINT written = 0;

    FRESULT fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
    printf("SD: f_open(%s) result: %s\n", filename, FRESULT_str(fr));
    if (fr != FR_OK) {
        printf("SD: f_open(%s) failed (%s)\n", filename, FRESULT_str(fr));
        neopixel_set_rgb(100, 0, 0);
        return false;
        // STORE TO FLASH****
    }

    fr = f_write(&fil, buf, len, &written);
    printf("SD: f_write(%s) wrote %lu bytes\n", filename, written);
    if (fr != FR_OK || written != len) {
        printf("SD: f_write failed (%u/%lu, %s)\n",
               written, (unsigned long)len, FRESULT_str(fr));
        (void)f_close(&fil);
        neopixel_set_rgb(100, 0, 0);
        return false;
        // STORE TO FLASH****
    }

    fr = f_close(&fil);
    if (fr != FR_OK) {
        printf("SD: f_close failed (%s)\n", FRESULT_str(fr));
        neopixel_set_rgb(100, 0, 0);
        return false;
        // STORE TO FLASH****
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


/*new functions*/
// static void init_run_directory(void) {
//     if (!g_sd_mounted) {
//         printf("RunDir: SD not mounted; no run directory.\n");
//         g_run_dir_name[0] = '\0';
//         g_run_index = 0;
//         g_sd_capture_index = 0;
//         return;
//     }

//     DIR dir;
//     FILINFO fno;
//     FRESULT fr;

//     fr = f_opendir(&dir, "0:");  // root of the SD card
//     if (fr != FR_OK) {
//         printf("RunDir: f_opendir failed (FR=%d)\n", fr);
//         g_run_dir_name[0] = '\0';
//         g_run_index = 0;
//         g_sd_capture_index = 0;
//         return;
//     }

//     uint32_t max_run_idx = 0;
//     bool found_any = false;
//     char digits[6];  // 5 digits + NUL

//     const char *RUN_PREFIX = "RUN_";
//     size_t rplen = strlen(RUN_PREFIX);

//     for (;;) {
//         fr = f_readdir(&dir, &fno);
//         if (fr != FR_OK || fno.fname[0] == 0) {
//             // error or end of directory
//             break;
//         }

//         // Only consider directories
//         if (!(fno.fattrib & AM_DIR)) continue;

//         const char *name = fno.fname;
//         size_t nlen = strlen(name);
//         if (nlen < rplen + 5) continue; // need at least "RUN_00000"

//         if (strncmp(name, RUN_PREFIX, rplen) != 0) continue;

//         const char *p_digits = name + rplen;
//         memcpy(digits, p_digits, 5);
//         digits[5] = '\0';

//         char *endptr = NULL;
//         long idx = strtol(digits, &endptr, 10);
//         if (*endptr != '\0' || idx < 0) {
//             continue;
//         }

//         if (!found_any || (uint32_t)idx > max_run_idx) {
//             max_run_idx = (uint32_t)idx;
//             found_any = true;
//         }
//     }

//     f_closedir(&dir);

//     if (found_any) {
//         g_run_index = max_run_idx + 1;
//     } else {
//         g_run_index = 0;
//     }

//     snprintf(g_run_dir_name, sizeof(g_run_dir_name),
//              "RUN_%05lu", (unsigned long)g_run_index);

//     printf("RunDir: creating %s\n", g_run_dir_name);

//     fr = f_mkdir(g_run_dir_name);
//     if (fr != FR_OK && fr != FR_EXIST) {
//         printf("RunDir: f_mkdir('%s') failed (FR=%d)\n", g_run_dir_name, fr);
//         // If this fails, we fall back to root
//         g_run_dir_name[0] = '\0';
//     }

//     // start file index at zero for this run
//     g_sd_capture_index = 0;

//     // Write metadata file into this run directory
//     if (g_run_dir_name[0]) {
//         write_run_metadata_file();
//     }
// }

// static void check_prefix_against_latest_run(void) {
//     if (!g_sd_mounted) return;

//     DIR dir;
//     FILINFO fno;
//     FRESULT fr;

//     fr = f_opendir(&dir, "0:");
//     if (fr != FR_OK) {
//         printf("PrefixCheck: f_opendir failed (FR=%d)\n", fr);
//         return;
//     }

//     const char *RUN_PREFIX = "RUN_";
//     size_t rplen = strlen(RUN_PREFIX);

//     uint32_t max_run_idx = 0;
//     bool found_any = false;
//     char digits[6];

//     // ----------- Scan root directory for highest RUN_XXXXX -----------
//     while (true) {
//         fr = f_readdir(&dir, &fno);
//         if (fr != FR_OK || fno.fname[0] == 0) break;

//         if (!(fno.fattrib & AM_DIR)) continue;

//         const char *name = fno.fname;
//         size_t nlen = strlen(name);
//         if (nlen < rplen + 5) continue;

//         if (strncmp(name, RUN_PREFIX, rplen) != 0) continue;

//         // pull out the 5 digits
//         memcpy(digits, name + rplen, 5);
//         digits[5] = '\0';

//         char *endptr;
//         long idx = strtol(digits, &endptr, 10);
//         if (*endptr != '\0' || idx < 0) continue;

//         if (!found_any || (uint32_t)idx > max_run_idx) {
//             max_run_idx = (uint32_t)idx;
//             found_any = true;
//         }
//     }

//     f_closedir(&dir);

//     if (!found_any) {
//         printf("PrefixCheck: no existing RUN directories found.\n");
//         return;  // nothing to compare against
//     }

//     // ---------- Build path to latest RUN_XXXXX/meta.txt ----------
//     char latest_run_dir[32];
//     snprintf(latest_run_dir, sizeof(latest_run_dir),
//              "RUN_%05lu", (unsigned long)max_run_idx);

//     char meta_path[64];
//     snprintf(meta_path, sizeof(meta_path),
//              "%s/meta.txt", latest_run_dir);

//     FIL meta;
//     fr = f_open(&meta, meta_path, FA_READ);
//     if (fr != FR_OK) {
//         printf("PrefixCheck: latest run %s has no meta.txt (FR=%d)\n",
//                latest_run_dir, fr);
//         return;
//     }

//     // ---------- Read PREFIX from meta.txt ----------
//     char line[64];
//     bool mismatch = false;

//     while (f_gets(line, sizeof(line), &meta)) {
//         trim_line(line);
//         if (strncmp(line, "PREFIX=", 7) == 0) {
//             const char *val = line + 7;
//             if (strncmp(val, g_file_prefix, FILE_PREFIX_MAX_LEN) != 0) {
//                 mismatch = true;
//             }
//             break;
//         }
//     }

//     f_close(&meta);

//     // ---------- Warning ----------
//     if (mismatch) {
//         printf("WARNING: prefix mismatch with latest run (%s)\n", latest_run_dir);
//         printf(" config.txt PREFIX=\"%s\"\n", g_file_prefix);
//         printf(" but latest meta.txt uses different prefix.\n");

//         for (int i = 0; i < 3; i++) {
//             neopixel_blink_once(0, 0, 255, 150);  // blue blink
//             sleep_ms(150);
//         }
//     }
// }

// static void init_sd_capture_index_from_existing_files(void) {
//     if (!g_sd_mounted) {
//         printf("Index: SD not mounted; starting at index 0.\n");
//         g_sd_capture_index = 0;
//         return;
//     }

//     DIR dir;
//     FILINFO fno;
//     FRESULT fr;

//     // Open root directory on logical drive 0:
//     fr = f_opendir(&dir, "0:");
//     if (fr != FR_OK) {
//         printf("Index: f_opendir failed (FR=%d); starting at index 0.\n", fr);
//         g_sd_capture_index = 0;
//         return;
//     }

//     uint32_t max_idx = 0;
//     bool found_any = false;
//     char digits[6];  // 5 digits + NUL

//     size_t plen = strlen(g_file_prefix);

//     for (;;) {
//         fr = f_readdir(&dir, &fno);
//         if (fr != FR_OK || fno.fname[0] == 0) {
//             // error or end of directory
//             break;
//         }

//         const char *name = fno.fname;
//         if (!name[0]) continue;

//         // We expect names like: PREFIX + 5 digits + ".bin"
//         // e.g. "RUNA_00012.bin"
//         size_t nlen = strlen(name);
//         if (nlen < plen + 5 + 4) continue;        // too short
//         if (strncmp(name, g_file_prefix, plen) != 0) continue;

//         // Check ".bin" suffix
//         const char *suffix = name + nlen - 4;
//         if (strcasecmp(suffix, ".bin") != 0) continue;

//         // Extract the 5-digit part right after the prefix
//         const char *p_digits = name + plen;
//         // Ensure there are at least 5 chars before ".bin"
//         if (p_digits + 5 > suffix) continue;

//         memcpy(digits, p_digits, 5);
//         digits[5] = '\0';

//         char *endptr = NULL;
//         long idx = strtol(digits, &endptr, 10);
//         if (*endptr != '\0' || idx < 0) {
//             continue;  // not pure digits
//         }

//         if (!found_any || (uint32_t)idx > max_idx) {
//             max_idx = (uint32_t)idx;
//             found_any = true;
//         }
//     }
    
//     f_closedir(&dir);

//     if (found_any) {
//         g_sd_capture_index = max_idx + 1;
//     } else {
//         g_sd_capture_index = 0;
//     }

//     printf("Index: starting capture index %lu (prefix \"%s\")\n",
//            (unsigned long)g_sd_capture_index, g_file_prefix);
// }

// static void write_run_metadata_file(void) {
//     if (!g_sd_mounted || g_run_dir_name[0] == '\0') {
//         return;
//     }

//     FIL fil;
//     char path[64];
//     FRESULT fr;

//     // Path: RUN_00012/meta.txt
//     snprintf(path, sizeof(path), "%s/meta.txt", g_run_dir_name);

//     fr = f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS);
//     if (fr != FR_OK) {
//         printf("Meta: f_open('%s') failed (FR=%d)\n", path, fr);
//         return;
//     }

//     char line[64];

//     // Basic run info
//     snprintf(line, sizeof(line), "RUN_INDEX=%lu\n", (unsigned long)g_run_index);
//     f_puts(line, &fil);

//     snprintf(line, sizeof(line), "SAMPLE_HZ=%u\n", g_target_sample_hz);
//     f_puts(line, &fil);

//     snprintf(line, sizeof(line), "PREFIX=%s\n", g_file_prefix);
//     f_puts(line, &fil);

//     snprintf(line, sizeof(line), "DATA_DEPTH=%u\n", CAPTURE_N_SAMPLES);
//     f_puts(line, &fil);

//     // Hardware config
//     snprintf(line, sizeof(line), "PIN_BASE=%u\n", CAPTURE_PIN_BASE);
//     f_puts(line, &fil);

//     snprintf(line, sizeof(line), "PIN_COUNT=%u\n", CAPTURE_PIN_COUNT);
//     f_puts(line, &fil);

//     snprintf(line, sizeof(line), "TRIGGER_PIN=%u\n", TRIGGER_PIN);
//     f_puts(line, &fil);

//     snprintf(line, sizeof(line), "TRIGGER_LEVEL=%u\n", TRIGGER_LEVEL ? 1u : 0u);
//     f_puts(line, &fil);

//     // You could add more here: build version, date stamp, etc.

//     f_close(&fil);
//     printf("Meta: wrote %s\n", path);
// }