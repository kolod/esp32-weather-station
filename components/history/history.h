#pragma once
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Start the history component.
 *        Subscribes to READING_UPDATED events, buffers 5-min samples,
 *        schedules hourly flush to LittleFS, and daily purge of old files.
 */
void history_start(void);

/**
 * @brief Return the approximate total number of records stored on flash.
 *        Thread-safe.
 */
uint32_t history_record_count(void);

/**
 * @brief Stream history records within [from, to] UTC epoch range to a callback.
 *        Records with invalid flag are omitted.
 * @param from     start epoch (0 = no lower bound)
 * @param to       end epoch (UINT32_MAX = no upper bound)
 * @param cb       called once per valid record in chronological order
 * @param ctx      user data passed to cb
 */
typedef void (*history_cb_t)(uint32_t epoch, float temp_c, void *ctx);
void history_query(uint32_t from, uint32_t to, history_cb_t cb, void *ctx);
