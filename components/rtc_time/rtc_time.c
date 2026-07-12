#include "rtc_time.h"
#include "rtc_time_priv.h"
#include "app_ctx.h"
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stddef.h>
#include <time.h>

#define TAG "rtc_time"

/* System time must be backed by the RTC timer or nothing survives a reset.
   (IDF 6 symbol, with the pre-picolibc alias accepted for older trees.) */
#if !defined(CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT) && !defined(CONFIG_LIBC_TIME_SYSCALL_USE_RTC) && \
    !defined(CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC_HRT) && !defined(CONFIG_NEWLIB_TIME_SYSCALL_USE_RTC)
#error "rtc_time requires the RTC timer as system time source (CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT=y)"
#endif

#ifndef BUILD_EPOCH
#error "BUILD_EPOCH must be injected by the rtc_time CMakeLists"
#endif

/* Upper plausibility bound: build date + ~20 years (data-model.md §2) */
#define RTC_TIME_VALID_WINDOW_S (20LL * 365 * 24 * 3600)

/* Survives everything except RTC-domain power loss — the same boundary as
   the clock itself, so validity and time can never disagree. */
static RTC_NOINIT_ATTR rtc_validity_record_t s_record;

uint32_t rtc_time_record_crc(const rtc_validity_record_t *rec)
{
    return esp_rom_crc32_le(0, (const uint8_t *)rec,
                            offsetof(rtc_validity_record_t, crc32));
}

bool rtc_time_record_intact(const rtc_validity_record_t *rec)
{
    return rec->magic == RTC_TIME_RECORD_MAGIC
        && rec->version == RTC_TIME_RECORD_VERSION
        && rec->crc32 == rtc_time_record_crc(rec);
}

int64_t rtc_time_min_valid(void)
{
    return BUILD_EPOCH;
}

int64_t rtc_time_max_valid(void)
{
    return BUILD_EPOCH + RTC_TIME_VALID_WINDOW_S;
}

bool rtc_time_restore_valid(bool intact, int64_t now)
{
    return intact
        && now >= rtc_time_min_valid()
        && now <= rtc_time_max_valid();
}

static void set_source(app_time_source_t src)
{
    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    app_state.time_source = src;
    xSemaphoreGive(app_state_mutex);
}

void rtc_time_restore(void)
{
    int64_t now    = (int64_t)time(NULL);
    bool    intact = rtc_time_record_intact(&s_record);

    if (rtc_time_restore_valid(intact, now)) {
        set_source(APP_TIME_SOURCE_RTC);
        app_event_post(APP_EVT_TIME_RESTORED);
        ESP_LOGI(TAG, "time restored from RTC: now=%lld, last NTP sync %lld",
                 (long long)now, (long long)s_record.last_sync_utc);
    } else if (!intact) {
        ESP_LOGI(TAG, "no valid time in RTC: record absent or corrupt (cold boot or dead battery)");
    } else if (now < rtc_time_min_valid()) {
        ESP_LOGI(TAG, "no valid time in RTC: now=%lld predates firmware build %lld",
                 (long long)now, (long long)rtc_time_min_valid());
    } else {
        ESP_LOGI(TAG, "no valid time in RTC: now=%lld beyond plausibility window %lld",
                 (long long)now, (long long)rtc_time_max_valid());
    }
}

void rtc_time_mark_synced(void)
{
    /* Invalidate first: a reset mid-update leaves a CRC-invalid record,
       which reads as "absent" — never as wrong data. */
    s_record.crc32         = 0;
    s_record.magic         = RTC_TIME_RECORD_MAGIC;
    s_record.version       = RTC_TIME_RECORD_VERSION;
    s_record.last_sync_utc = (int64_t)time(NULL);
    s_record.crc32         = rtc_time_record_crc(&s_record);

    set_source(APP_TIME_SOURCE_NTP);
}

app_time_source_t rtc_time_source(void)
{
    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    app_time_source_t src = app_state.time_source;
    xSemaphoreGive(app_state_mutex);
    return src;
}

int64_t rtc_time_last_sync(void)
{
    return rtc_time_record_intact(&s_record) ? s_record.last_sync_utc : -1;
}
