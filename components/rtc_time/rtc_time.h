#pragma once

#include <stdint.h>
#include "app_ctx.h"

/**
 * @file rtc_time.h
 * @brief Battery-backed time restore (feature 002).
 *
 * System time survives every reset while the ESP32 RTC domain stays powered
 * (module battery). This component decides at boot whether that surviving
 * time is trustworthy — a validity record in RTC slow memory, written on
 * every successful SNTP sync, plus plausibility bounds against the firmware
 * build date — and publishes the time-source state in app_state.
 */

/**
 * @brief Boot-time restore check. Call exactly once, after NVS/event-loop
 *        init and before the display and web_server tasks start.
 *
 * On success sets app_state.time_source = APP_TIME_SOURCE_RTC and posts
 * APP_EVT_TIME_RESTORED. Never modifies system time, never blocks, never
 * fails — a rejected restore (reason logged at INFO) simply leaves
 * APP_TIME_SOURCE_NONE, which is the pre-existing "time not available"
 * behavior.
 */
void rtc_time_restore(void);

/**
 * @brief Record a successful network time synchronization.
 *
 * Call from the SNTP sync callback after the system clock has been updated.
 * Rewrites the battery-backed validity record (CRC last, so a reset
 * mid-update reads as "absent", never as wrong data) and sets
 * app_state.time_source = APP_TIME_SOURCE_NTP. Idempotent.
 */
void rtc_time_mark_synced(void);

/** @brief Current time-source state (thread-safe accessor). */
app_time_source_t rtc_time_source(void);

/**
 * @brief Unix seconds (UTC) of the most recent recorded network sync,
 *        or -1 if no intact validity record exists.
 */
int64_t rtc_time_last_sync(void);
