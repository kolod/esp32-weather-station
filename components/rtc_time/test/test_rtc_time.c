#include "unity.h"
#include "rtc_time.h"
#include "rtc_time_priv.h"
#include "app_ctx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <sys/time.h>
#include <time.h>

static rtc_validity_record_t make_record(int64_t last_sync)
{
    rtc_validity_record_t r = {
        .magic         = RTC_TIME_RECORD_MAGIC,
        .version       = RTC_TIME_RECORD_VERSION,
        .last_sync_utc = last_sync,
    };
    r.crc32 = rtc_time_record_crc(&r);
    return r;
}

TEST_CASE("intact record validates", "[rtc_time]")
{
    rtc_validity_record_t r = make_record(1783947600);
    TEST_ASSERT_TRUE(rtc_time_record_intact(&r));
}

TEST_CASE("garbage record rejected", "[rtc_time]")
{
    rtc_validity_record_t r;
    memset(&r, 0xA5, sizeof(r));
    TEST_ASSERT_FALSE(rtc_time_record_intact(&r));
    memset(&r, 0x00, sizeof(r));
    TEST_ASSERT_FALSE(rtc_time_record_intact(&r));
}

TEST_CASE("wrong magic or version rejected", "[rtc_time]")
{
    rtc_validity_record_t r = make_record(1783947600);
    r.magic ^= 1;
    r.crc32 = rtc_time_record_crc(&r); /* even with a matching CRC */
    TEST_ASSERT_FALSE(rtc_time_record_intact(&r));

    r = make_record(1783947600);
    r.version = RTC_TIME_RECORD_VERSION + 1;
    r.crc32 = rtc_time_record_crc(&r);
    TEST_ASSERT_FALSE(rtc_time_record_intact(&r));
}

TEST_CASE("torn write rejected", "[rtc_time]")
{
    /* mark_synced zeroes the CRC before touching the payload; a reset at any
       point mid-update must therefore read as corrupt */
    rtc_validity_record_t r = make_record(1783947600);
    r.crc32 = 0;
    TEST_ASSERT_FALSE(rtc_time_record_intact(&r));

    r = make_record(1783947600);
    r.last_sync_utc += 1; /* payload changed, CRC not yet rewritten */
    TEST_ASSERT_FALSE(rtc_time_record_intact(&r));
}

TEST_CASE("restore decision truth table", "[rtc_time]")
{
    const int64_t min = rtc_time_min_valid();
    const int64_t max = rtc_time_max_valid();
    TEST_ASSERT_TRUE(min > 0 && max > min);

    /* corrupt record: never valid, even with plausible time */
    TEST_ASSERT_FALSE(rtc_time_restore_valid(false, min + 1000));

    /* intact record: bounds decide (FR-004) */
    TEST_ASSERT_FALSE(rtc_time_restore_valid(true, min - 1));
    TEST_ASSERT_TRUE (rtc_time_restore_valid(true, min));
    TEST_ASSERT_TRUE (rtc_time_restore_valid(true, min + 1000));
    TEST_ASSERT_TRUE (rtc_time_restore_valid(true, max));
    TEST_ASSERT_FALSE(rtc_time_restore_valid(true, max + 1));

    /* epoch-zero (never-set clock) always rejected */
    TEST_ASSERT_FALSE(rtc_time_restore_valid(true, 0));
}

TEST_CASE("mark_synced refreshes record, last_sync monotonic, state NTP", "[rtc_time]")
{
    if (app_state_mutex == NULL) {
        app_state_mutex = xSemaphoreCreateMutex();
    }

    const int64_t t1 = rtc_time_min_valid() + 1000;
    struct timeval tv = {.tv_sec = (time_t)t1, .tv_usec = 0};
    TEST_ASSERT_EQUAL(0, settimeofday(&tv, NULL));

    rtc_time_mark_synced();
    int64_t s1 = rtc_time_last_sync();
    TEST_ASSERT_INT64_WITHIN(2, t1, s1);
    TEST_ASSERT_EQUAL(APP_TIME_SOURCE_NTP, rtc_time_source());

    /* periodic re-sync with a later time keeps the record intact and advances it */
    const int64_t t2 = t1 + 3600;
    tv.tv_sec = (time_t)t2;
    TEST_ASSERT_EQUAL(0, settimeofday(&tv, NULL));

    rtc_time_mark_synced();
    int64_t s2 = rtc_time_last_sync();
    TEST_ASSERT_TRUE(s2 > s1);
    TEST_ASSERT_INT64_WITHIN(2, t2, s2);
    TEST_ASSERT_EQUAL(APP_TIME_SOURCE_NTP, rtc_time_source());
}
