#include "rtc.h"
#include <stdio.h>

// ── set time ─────────────────────────────────────────────────────────────────

void mcp_set_time(i2c_inst_t *i2c, uint8_t hours, uint8_t minutes, uint8_t seconds) {
    uint8_t buf[4];
    buf[0] = REG_RTCSEC;
    buf[1] = dec_to_bcd(seconds) | 0x80;   // ST bit → starts oscillator
    buf[2] = dec_to_bcd(minutes);
    buf[3] = dec_to_bcd(hours) & 0x3F;     // ensure 24hr mode
    printf("RTC: write to register %02u: %02u:%02u:%02u\n", REG_RTCSEC, hours, minutes, seconds);
    i2c_write_blocking(i2c, RTC_ADDR, buf, 4, false);
    printf("RTC: set time to %02u:%02u:%02u\n", hours, minutes, seconds);
}

// ── get time ─────────────────────────────────────────────────────────────────

trigger_time_t rtc_get_time(i2c_inst_t *i2c) {
    trigger_time_t t = {0};

    uint8_t reg = REG_RTCSEC;
    uint8_t raw[4] = {0};

    int wr = i2c_write_blocking(i2c, RTC_ADDR, &reg, 1, true);   // point to 0x00, repeated start
    if (wr != 1) {
        printf("RTC read failed: pointer write returned %d (expected 1)\n", wr);
        return t;
    }

    int rd = i2c_read_blocking(i2c, RTC_ADDR, raw, 4, false);    // burst read SEC, MIN, HOUR, WKDAY
    if (rd != 4) {
        printf("RTC read failed: data read returned %d (expected 4)\n", rd);
        return t;
    }

    t.year  = 0;
    t.month = 0;
    t.sec   = bcd_to_dec(raw[0] & 0x7F);   // mask ST bit
    t.min   = bcd_to_dec(raw[1] & 0x7F);
    t.hour  = bcd_to_dec(raw[2] & 0x3F);   // mask 12/24h + AM/PM mode bits
    t.day   = bcd_to_dec(raw[3] & 0x07);   // WKDAY (day-of-week 1-7); mask OSCRUN/PWRFAIL/VBATEN

    return t;
}
