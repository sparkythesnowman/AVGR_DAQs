#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Initialize SD driver and mount "0:". Returns true on success. */
bool sd_init(void); 

/* Write one capture buffer to sequential file "0:cap_NNNN.bin". */
bool write_capture_to_sd(const uint8_t *buf, uint32_t len);

/* Unmount "0:" and release the SD card. Call before halting or reset. */
void sd_unmount(void);
