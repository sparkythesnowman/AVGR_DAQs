#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
} trigger_time_t;

/* Initialize SD driver and mount "0:". Returns true on success. */
bool sd_init(void); 

// static void trim_line(char *s) {
//     // Strip trailing \r\n and spaces
//     size_t len = strlen(s);
//     while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t')) {
//         s[--len] = '\0';
//     }
// }

/* Load config from SD*/
// static config_state_t load_config_from_sd(void);

/* Write one capture buffer to "0:cap_day_hour_min_sec.bin". */
bool write_capture_to_sd(const uint8_t *buf, uint32_t len, const trigger_time_t *trigger_time, uint8_t daq_num);

/* Unmount "0:" and release the SD card. Call before halting or reset. */
void sd_unmount(void);


// static void init_run_directory(void);
// static void check_prefix_against_latest_run(void);
// static void init_sd_capture_index_from_existing_files(void);
// static void write_run_metadata_file(void);
