#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Internal pieces of rtc_time, exposed for unit tests (pure logic only). */

#define RTC_TIME_RECORD_MAGIC   0x52544301u /* "RTC" + record version 1 */
#define RTC_TIME_RECORD_VERSION 1u

/* Battery-backed validity record (data-model.md §1). Lives in RTC slow
   memory, the same power domain as the clock — both survive or both perish. */
typedef struct {
    uint32_t magic;
    uint32_t version;
    int64_t  last_sync_utc; /* Unix seconds, UTC, of last successful SNTP sync */
    uint32_t crc32;         /* over all preceding fields */
} rtc_validity_record_t;

uint32_t rtc_time_record_crc(const rtc_validity_record_t *rec);
bool     rtc_time_record_intact(const rtc_validity_record_t *rec);

/* Plausibility bounds derived from the injected BUILD_EPOCH (data-model.md §2) */
int64_t rtc_time_min_valid(void);
int64_t rtc_time_max_valid(void);

/* Pure restore decision (data-model.md §3) */
bool rtc_time_restore_valid(bool intact, int64_t now);
