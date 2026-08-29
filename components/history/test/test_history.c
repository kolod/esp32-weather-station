#include "unity.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* ── Inline the record types and CRC helper for self-contained tests ──
   Layouts mirror history.c: v1 (8 B, pre-005 "YYYYMM.bin" files) and
   v2 (12 B, feature-005 "YYYYMM.bi2" files with pressure; feature 007 spends
   the 2 reserved bytes on humidity without changing size/name/CRC coverage). */
typedef struct __attribute__((packed)) {
    uint32_t epoch;
    int16_t  temp_centi;
    uint8_t  flags;
    uint8_t  crc8;
} hist_record_t;

typedef struct __attribute__((packed)) {
    uint32_t epoch;        /* off 0  */
    int16_t  temp_centi;   /* off 4  */
    uint16_t press_deci;   /* off 6  hPa × 10; meaningful if flags bit1 */
    uint8_t  flags;        /* off 8  bit0 temp | bit1 pressure | bit2 humidity */
    uint16_t hum_centi;    /* off 9  %RH × 100; meaningful if flags bit2 (was reserved[2]) */
    uint8_t  crc8;         /* off 11 CRC-8 over bytes [0..10] */
} hist_record_v2_t;

/* The pre-007 v2 layout, byte-compatible, used to prove old records still read. */
typedef struct __attribute__((packed)) {
    uint32_t epoch;
    int16_t  temp_centi;
    uint16_t press_deci;
    uint8_t  flags;
    uint8_t  reserved[2];
    uint8_t  crc8;
} hist_record_v2_pre007_t;

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

/* ── v2 records (feature 005: pressure) ── */

static hist_record_v2_t make_record_v2(uint32_t epoch, float temp_c, bool valid,
                                       float press_hpa, bool press_valid,
                                       float hum_pct, bool hum_valid)
{
    hist_record_v2_t r = {0};
    r.epoch      = epoch;
    r.temp_centi = (int16_t)(temp_c * 100.0f);
    r.flags      = valid ? 0x01 : 0x00;
    if (press_valid && press_hpa >= 300.0f && press_hpa <= 1100.0f) {
        r.press_deci = (uint16_t)(press_hpa * 10.0f + 0.5f);
        r.flags     |= 0x02;
    }
    if (hum_valid && hum_pct >= 0.0f && hum_pct <= 100.0f) {
        r.hum_centi = (uint16_t)(hum_pct * 100.0f + 0.5f);
        r.flags    |= 0x04;
    }
    r.crc8 = crc8((uint8_t *)&r, offsetof(hist_record_v2_t, crc8));
    return r;
}

TEST_CASE("history v2: record encodes 12 bytes", "[history]")
{
    TEST_ASSERT_EQUAL(12, sizeof(hist_record_v2_t));
}

TEST_CASE("history v2: pressure round-trips at 0.1 hPa", "[history]")
{
    hist_record_v2_t r = make_record_v2(1783190400UL, 21.5f, true,
                                        1013.2f, true, 0.0f, false);
    TEST_ASSERT_EQUAL(1, r.flags & 0x01);
    TEST_ASSERT_EQUAL(0x02, r.flags & 0x02);
    TEST_ASSERT_EQUAL(10132, r.press_deci);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1013.2f, (float)r.press_deci / 10.0f);
}

TEST_CASE("history v2: absent pressure clears bit1, never encodes zero", "[history]")
{
    hist_record_v2_t r = make_record_v2(1783190400UL, 21.5f, true,
                                        0.0f, false, 0.0f, false);
    TEST_ASSERT_EQUAL(0, r.flags & 0x02);
    TEST_ASSERT_EQUAL(0, r.press_deci);
    /* out-of-range pressure is treated as absent even if flagged valid */
    r = make_record_v2(1783190400UL, 21.5f, true, 2000.0f, true, 0.0f, false);
    TEST_ASSERT_EQUAL(0, r.flags & 0x02);
}

TEST_CASE("history v2: CRC covers pressure bytes", "[history]")
{
    hist_record_v2_t r = make_record_v2(1783190400UL, 21.5f, true,
                                        1013.2f, true, 0.0f, false);
    uint8_t expected = crc8((uint8_t *)&r, offsetof(hist_record_v2_t, crc8));
    TEST_ASSERT_EQUAL(expected, r.crc8);
    r.press_deci ^= 1;
    TEST_ASSERT_NOT_EQUAL(crc8((uint8_t *)&r, offsetof(hist_record_v2_t, crc8)),
                          r.crc8);
}

TEST_CASE("history v2: v1 record still decodes (pressure + humidity absent)", "[history]")
{
    /* A v1 record read from a .bin file: temperature decodes, no bit1/bit2 —
       the reader reports pressure and humidity as absent (NAN at the callback). */
    hist_record_t v1 = make_record(1783190400UL, 23.45f, true);
    TEST_ASSERT_EQUAL(8, sizeof(v1));
    TEST_ASSERT_EQUAL(2345, v1.temp_centi);
    TEST_ASSERT_EQUAL(0, v1.flags & 0x06); /* bits 1 and 2 never set by v1 writers */
}

/* ── v2 humidity (feature 007) ── */

TEST_CASE("history v2: humidity spends the reserved bytes, size unchanged", "[history]")
{
    TEST_ASSERT_EQUAL(12, sizeof(hist_record_v2_t));
    TEST_ASSERT_EQUAL(sizeof(hist_record_v2_pre007_t), sizeof(hist_record_v2_t));
    /* flags and crc8 sit at the same offsets as before → old records readable */
    TEST_ASSERT_EQUAL(offsetof(hist_record_v2_pre007_t, flags),
                      offsetof(hist_record_v2_t, flags));
    TEST_ASSERT_EQUAL(offsetof(hist_record_v2_pre007_t, crc8),
                      offsetof(hist_record_v2_t, crc8));
}

TEST_CASE("history v2: humidity round-trips at 0.1 %RH", "[history]")
{
    hist_record_v2_t r = make_record_v2(1783190400UL, 21.5f, true,
                                        1013.2f, true, 47.5f, true);
    TEST_ASSERT_EQUAL(0x04, r.flags & 0x04);
    TEST_ASSERT_EQUAL(4750, r.hum_centi);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 47.5f, (float)r.hum_centi / 100.0f);
}

TEST_CASE("history v2: saturated and bone-dry humidity are valid", "[history]")
{
    hist_record_v2_t wet = make_record_v2(0, 20.0f, true, 1000.0f, true, 100.0f, true);
    TEST_ASSERT_EQUAL(0x04, wet.flags & 0x04);
    TEST_ASSERT_EQUAL(10000, wet.hum_centi);
    hist_record_v2_t dry = make_record_v2(0, 20.0f, true, 1000.0f, true, 0.0f, true);
    TEST_ASSERT_EQUAL(0x04, dry.flags & 0x04);
    TEST_ASSERT_EQUAL(0, dry.hum_centi);
}

TEST_CASE("history v2: absent humidity clears bit2, never encodes zero-as-value", "[history]")
{
    hist_record_v2_t r = make_record_v2(0, 20.0f, true, 1000.0f, true, 55.0f, false);
    TEST_ASSERT_EQUAL(0, r.flags & 0x04);
    TEST_ASSERT_EQUAL(0, r.hum_centi);
    /* out-of-range humidity treated as absent even if flagged valid */
    r = make_record_v2(0, 20.0f, true, 1000.0f, true, 150.0f, true);
    TEST_ASSERT_EQUAL(0, r.flags & 0x04);
}

TEST_CASE("history v2: CRC covers humidity bytes", "[history]")
{
    hist_record_v2_t r = make_record_v2(0, 20.0f, true, 1000.0f, true, 47.5f, true);
    TEST_ASSERT_EQUAL(r.crc8, crc8((uint8_t *)&r, offsetof(hist_record_v2_t, crc8)));
    r.hum_centi ^= 1;
    TEST_ASSERT_NOT_EQUAL(crc8((uint8_t *)&r, offsetof(hist_record_v2_t, crc8)),
                          r.crc8);
}

TEST_CASE("history v2: pre-007 record decodes with humidity absent", "[history]")
{
    /* An old .bi2 record: reserved bytes zero, bit2 clear → humidity absent,
       and it still CRCs identically under the new struct (same 11 bytes). */
    hist_record_v2_pre007_t old = {0};
    old.epoch      = 1783190400UL;
    old.temp_centi = 2150;
    old.press_deci = 10132;
    old.flags      = 0x01 | 0x02;
    old.crc8       = crc8((uint8_t *)&old, offsetof(hist_record_v2_pre007_t, crc8));

    hist_record_v2_t as_new;
    memcpy(&as_new, &old, sizeof(as_new));
    TEST_ASSERT_EQUAL(as_new.crc8,
                      crc8((uint8_t *)&as_new, offsetof(hist_record_v2_t, crc8)));
    TEST_ASSERT_EQUAL(0, as_new.flags & 0x04); /* humidity absent */
    TEST_ASSERT_EQUAL(0, as_new.hum_centi);
    TEST_ASSERT_EQUAL(10132, as_new.press_deci);
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
