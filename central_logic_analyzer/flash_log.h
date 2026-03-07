#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "hardware/flash.h"

#define LOG_REGION_SIZE   (256u * 1024u)
#define LOG_OFFSET        (PICO_FLASH_SIZE_BYTES - LOG_REGION_SIZE)
#define HEADER_STORE_LEN  256u

#define CAPTURE_N_SAMPLES 131072u  // logical samples per pin (64 kB)

bool logic_has_saved_capture(void);
void logic_dump_saved_capture_hex(void);
void logic_save_capture_to_flash_raw(const uint32_t *cap_words,
    uint32_t sample_count, uint32_t word_count,
    uint32_t periph_index, uint32_t sample_hz);
void logic_erase_saved_capture(void);
