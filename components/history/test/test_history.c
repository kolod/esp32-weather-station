#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ── Inline the record type and CRC helper for self-contained tests ── */
typedef struct __attribute__((packed)) {
    uint32_t epoch;
    int16_t  temp_centi;
    uint8_t  flags;
    uint8_t  crc8;
} hist_record_t;

static uint8_t crc8(const uint8_t *d, size_t n)
{
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++)
            c = (c & 0x80) ? (c<<1)^0x07 : (c<<1);
    }
    return c;
}

static hist_record_t make_record(uint32_t epoch, float temp_c, bool valid)
{
    hist_record_t r;
    r.epoch      = epoch;
    r.temp_centi = (int16_t)(temp_c * 100.0f);
    r.flags      = valid ? 0x01 : 0x00;
    r.crc8       = crc8((uint8_t*)&r, offsetof(hist_record_t, crc8));
    return r;
}

TEST_CASE("history: record encodes 8 bytes", "[history]")
{
    TEST_ASSERT_EQUAL(8, sizeof(hist_record_t));
}

TEST_CASE("history: CRC round-trip", "[history]")
{
    hist_record_t r = make_record(1783190400UL, 23.45f, true);
    uint8_t expected = crc8((uint8_t*)&r, offsetof(hist_record_t, crc8));
    TEST_ASSERT_EQUAL(expected, r.crc8);
    /* Mutate a byte — CRC must differ */
    r.temp_centi ^= 1;
    uint8_t bad = crc8((uint8_t*)&r, offsetof(hist_record_t, crc8));
    TEST_ASSERT_NOT_EQUAL(bad, r.crc8);
}

TEST_CASE("history: invalid record has bit0=0 in flags", "[history]")
{
    hist_record_t r = make_record(1783190400UL, 0.0f, false);
    TEST_ASSERT_EQUAL(0, r.flags & 0x01);
}

TEST_CASE("history: valid record has bit0=1 in flags", "[history]")
{
    hist_record_t r = make_record(1783190400UL, 21.5f, true);
    TEST_ASSERT_EQUAL(1, r.flags & 0x01);
}

TEST_CASE("history: temp_centi encodes correctly", "[history]")
{
    hist_record_t r = make_record(0, 23.45f, true);
    /* 23.45 * 100 = 2345 */
    TEST_ASSERT_EQUAL(2345, r.temp_centi);
}

TEST_CASE("history: negative temperature encodes correctly", "[history]")
{
    hist_record_t r = make_record(0, -12.30f, true);
    TEST_ASSERT_EQUAL(-1230, r.temp_centi);
}

/* ── Purge selection logic (deterministic, no FS) ── */

typedef struct { int year; int month; } ym_t;

static bool should_purge(ym_t file, uint32_t now_epoch)
{
    /* Compute approximate end-of-month for the file month */
    struct tm tm = {
        .tm_year = file.year - 1900,
        .tm_mon  = file.month,  /* .tm_mon is 0-based; file.month is 1-based so +1 = start of next month */
        .tm_mday = 1,
        .tm_hour = 0,
    };
    /* End of month = start of next month (approximated) */
    time_t end_of_month = mktime(&tm);
    time_t cutoff = (time_t)now_epoch - (time_t)(3 * 31 * 24 * 3600);
    return end_of_month < cutoff;
}

TEST_CASE("history purge: old month (> 3 months) is purged", "[history]")
{
    /* now = 2026-07-11 → cutoff ≈ 2026-04-10; 2026-02 should be purged */
    uint32_t now = 1752192000UL; /* 2026-07-11 00:00 UTC */
    TEST_ASSERT_TRUE(should_purge((ym_t){2026, 2}, now));
}

TEST_CASE("history purge: recent month (< 3 months) is kept", "[history]")
{
    uint32_t now = 1752192000UL;
    TEST_ASSERT_FALSE(should_purge((ym_t){2026, 5}, now));
}

TEST_CASE("history purge: current month is kept", "[history]")
{
    uint32_t now = 1752192000UL;
    TEST_ASSERT_FALSE(should_purge((ym_t){2026, 7}, now));
}
