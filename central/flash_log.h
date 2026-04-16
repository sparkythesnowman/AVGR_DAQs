#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Open a new numbered capture file (e.g. "cap_0001.bin") on the SD card.
 * Returns true on success. Must be called before flash_log_write(). */
bool flash_log_open_new(void);

/* Append 'len' bytes from 'data' to the currently open capture file.
 * Returns true on success. */
bool flash_log_write(const uint8_t *data, uint32_t len);

/* Flush and close the current capture file. */
void flash_log_close(void);

/* Return the index of the last file written (increments each open). */
uint32_t flash_log_get_index(void);