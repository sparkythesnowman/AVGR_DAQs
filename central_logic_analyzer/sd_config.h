#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CONFIG_STATE_NO_SD,          // SD not mounted / unusable
    CONFIG_STATE_NO_FILE,        // SD OK, but no config.txt
    CONFIG_STATE_MATCH,          // SD config matches flash config
    CONFIG_STATE_NEW_OR_CHANGED  // no flash config OR mismatch with SD config
} config_state_t;

void sd_init_if_needed(void);
config_state_t load_config_from_sd(void);
void init_run_directory(uint32_t periph_count);
void init_sd_capture_index_for_flat_mode(void);
void check_prefix_against_latest_run(void);

bool capture_to_sd(const uint32_t *cap_words,
                   uint32_t word_count,
                   uint32_t periph_index,
                   uint32_t sample_hz);

uint32_t sd_get_target_sample_hz(void);
const char *sd_get_file_prefix(void);
const char *sd_get_run_dir_name(void);
uint32_t sd_get_capture_index(void);
