/**
 * Flash-backed capture storage
 *
 * When SD write fails, captures are saved to the last 256 kB of on-chip flash.
 * - logic_has_saved_capture(): true if valid header present at LOG_OFFSET
 * - logic_dump_saved_capture_hex(): output via printf for USB serial export
 * - logic_save_capture_to_flash_raw(): write header + data
 * - logic_erase_saved_capture(): wipe region (used after 'e' over USB)
 */

#include "flash_log.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>

/* Header at LOG_OFFSET; data follows at LOG_OFFSET + HEADER_STORE_LEN */
typedef struct {
    uint32_t magic;         /* 'LAC1' = 0x3143414C */
    uint16_t version;       /* 1 */
    uint16_t hdr_len;       /* sizeof(header) */
    uint32_t sample_count;  /* Logical samples per pin */
    uint32_t sample_hz;     /* Effective sample rate */
    uint32_t periph_index;  /* Which DAQ board */
    uint32_t word_count;    /* 32-bit words stored following header */
    uint32_t reserved0;
} logic_log_header_t;

static const uint32_t LOG_MAGIC = 0x3143414Cu; /* 'LAC1' */

static inline uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1) & ~(a - 1); }

/* Check if a valid capture header exists at LOG_OFFSET (XIP_BASE + flash addr). */
bool logic_has_saved_capture(void) {
    const logic_log_header_t *hdr = (const logic_log_header_t *)(XIP_BASE + LOG_OFFSET);
    if (hdr->magic != LOG_MAGIC || hdr->version != 1) return false;
    if (hdr->word_count == 0 || hdr->word_count > (LOG_REGION_SIZE / 4)) return false;
    return true;
}

/* Output header + hex dump of data to printf (for USB serial export). */
void logic_dump_saved_capture_hex(void) {
    const logic_log_header_t *hdr = (const logic_log_header_t *)(XIP_BASE + LOG_OFFSET);
    if (hdr->magic != LOG_MAGIC) { printf("NOLOG\n"); return; }

    uint32_t expected_words = CAPTURE_N_SAMPLES/4;
    uint32_t max_words = (LOG_REGION_SIZE - HEADER_STORE_LEN) / 4;
    uint32_t words = hdr->word_count;
    if (words > expected_words) words = expected_words;
    if (words > max_words) words = max_words;

    printf("HDR magic=LAC1 ver=%u samples=%u hz=%u words=%u\n",
        hdr->version, hdr->sample_count, hdr->sample_hz,
        words);

    const uint32_t *data = (const uint32_t *)(XIP_BASE + LOG_OFFSET + HEADER_STORE_LEN);
    for (uint32_t i = 0; i < words; ++i) {
        printf("%08X\n", data[i]);
    }
    printf("END\n");
}

/* Erase LOG_REGION, program header then data in page-sized chunks. Disables IRQs during flash ops. */
void logic_save_capture_to_flash_raw(const uint32_t *cap_words,
    uint32_t sample_count,
    uint32_t word_count,
    uint32_t periph_index,
    uint32_t sample_hz)
{
    logic_log_header_t hdr = {0};
    hdr.magic         = LOG_MAGIC;
    hdr.version       = 1;
    hdr.hdr_len       = sizeof(hdr);
    hdr.sample_count  = sample_count;  // logical samples
    hdr.periph_index  = periph_index;
    hdr.sample_hz     = sample_hz;
    hdr.word_count    = word_count;    // 32-bit words

    uint8_t header_block[HEADER_STORE_LEN];
    memset(header_block, 0xFF, sizeof(header_block));
    memcpy(header_block, &hdr, sizeof(hdr));

    const uint32_t data_bytes = word_count * sizeof(uint32_t);
    const uint32_t prog_len   = HEADER_STORE_LEN + align_up(data_bytes, HEADER_STORE_LEN);
    const uint32_t erase_len  = align_up(prog_len, FLASH_SECTOR_SIZE);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(LOG_OFFSET, erase_len);
    flash_range_program(LOG_OFFSET, header_block, HEADER_STORE_LEN);

    uint32_t written = 0;
    uint32_t addr = LOG_OFFSET + HEADER_STORE_LEN;
    while (written < data_bytes) {
        uint8_t page[HEADER_STORE_LEN];
        uint32_t chunk = (data_bytes - written >= HEADER_STORE_LEN) ? HEADER_STORE_LEN : (data_bytes - written);
        memset(page, 0xFF, sizeof(page));
        memcpy(page, ((const uint8_t *)cap_words) + written, chunk);
        flash_range_program(addr, page, HEADER_STORE_LEN);
        addr    += HEADER_STORE_LEN;
        written += chunk;
    }
    restore_interrupts(ints);
}

/* Erase entire LOG_REGION (called after user sends 'e' over USB). */
void logic_erase_saved_capture(void) {
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(LOG_OFFSET, LOG_REGION_SIZE);
    restore_interrupts(ints);
    printf("ERASED\n");
}