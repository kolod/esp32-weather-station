#include "history.h"
#include "app_ctx.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define TAG           "history"
#define HIST_DIR      "/storage/history"
#define SAMPLE_SECS   (5 * 60)   /* sample every 5 minutes */
#define FLUSH_SECS    (60 * 60)  /* flush once per hour     */
#define PURGE_SECS    (24 * 3600)/* purge check once per day */
#define RING_SIZE     12         /* max 12 samples per hour  */
#define MONTHS_RETAIN 3

/* ── 8-byte on-disk record ── */
typedef struct __attribute__((packed)) {
    uint32_t epoch;        /* UTC seconds                     */
    int16_t  temp_centi;   /* °C × 100                        */
    uint8_t  flags;        /* bit0: valid                     */
    uint8_t  crc8;         /* CRC-8 over bytes [0..6] poly 0x07 */
} hist_record_t;

_Static_assert(sizeof(hist_record_t) == 8, "record must be 8 bytes");

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

/* ── RAM ring buffer (current hour's samples) ── */
static hist_record_t s_ring[RING_SIZE];
static int           s_ring_count = 0;
static SemaphoreHandle_t s_ring_mutex = NULL;

static time_t s_last_sample_time = 0;
static time_t s_last_flush_time  = 0;
static time_t s_last_purge_time  = 0;

/* Approximate total record count cached to avoid full FS scan on every /api/status */
static volatile uint32_t s_record_count = 0;

/* ── File helpers ── */
static void path_for_month(time_t t, char *buf, size_t len)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(buf, len, "%s/%04d%02d.bin",
             HIST_DIR, tm.tm_year + 1900, tm.tm_mon + 1);
}

/* ── Flush buffered samples to flash ── */
static void flush_ring(void)
{
    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
    if (s_ring_count == 0) { xSemaphoreGive(s_ring_mutex); return; }

    char path[64];
    path_for_month(time(NULL), path, sizeof(path));

    FILE *f = fopen(path, "ab");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open %s for append", path);
        xSemaphoreGive(s_ring_mutex);
        return;
    }
    fwrite(s_ring, sizeof(hist_record_t), s_ring_count, f);
    fclose(f);

    s_record_count += s_ring_count;
    ESP_LOGI(TAG, "Flushed %d records to %s", s_ring_count, path);
    s_ring_count = 0;
    xSemaphoreGive(s_ring_mutex);
}

/* ── Record a new sample ── */
static void record_sample(float temp_c, bool valid)
{
    hist_record_t r;
    r.epoch      = (uint32_t)time(NULL);
    r.temp_centi = (int16_t)(temp_c * 100.0f);
    r.flags      = valid ? 0x01 : 0x00;
    r.crc8       = crc8((uint8_t *)&r, offsetof(hist_record_t, crc8));

    xSemaphoreTake(s_ring_mutex, portMAX_DELAY);
    if (s_ring_count < RING_SIZE)
        s_ring[s_ring_count++] = r;
    xSemaphoreGive(s_ring_mutex);
}

/* ── Daily purge: unlink files whose entire month is outside 3-month window ── */
static void purge_old(void)
{
    time_t now = time(NULL);
    /* Three months ago (approximate) */
    time_t cutoff = now - (time_t)(MONTHS_RETAIN) * 31 * 24 * 3600;

    struct tm cut_tm;
    gmtime_r(&cutoff, &cut_tm);
    int cut_year  = cut_tm.tm_year + 1900;
    int cut_month = cut_tm.tm_mon + 1;

    DIR *d = opendir(HIST_DIR);
    if (!d) return;
    struct dirent *ent;
    uint32_t count = 0;
    while ((ent = readdir(d)) != NULL) {
        int y, m;
        if (sscanf(ent->d_name, "%4d%2d.bin", &y, &m) != 2) continue;
        /* Count all records for live stats */
        char path[32];
        snprintf(path, sizeof(path), "%s/%04d%02d.bin", HIST_DIR, y, m);
        struct stat st;
        if (stat(path, &st) == 0)
            count += (uint32_t)(st.st_size / sizeof(hist_record_t));

        /* Delete if entire month ended before cutoff */
        bool old = (y < cut_year) || (y == cut_year && m < cut_month);
        if (old) {
            ESP_LOGI(TAG, "Purging %s", path);
            unlink(path);
        }
    }
    closedir(d);
    s_record_count = count;
    ESP_LOGI(TAG, "Purge complete; ~%lu records on disk", (unsigned long)s_record_count);
}

/* ── Event handler: check whether to sample, flush, or purge ── */
static void on_reading(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    time_t now = time(NULL);
    /* No trustworthy time yet (neither NTP sync nor RTC restore) — a restored
       clock is always ≥ the firmware build epoch, so it passes this gate. */
    if (now < 1000000) return;

    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    temperature_reading_t r = app_state.reading;
    xSemaphoreGive(app_state_mutex);

    /* 5-minute sample */
    if (now - s_last_sample_time >= SAMPLE_SECS) {
        s_last_sample_time = now;
        record_sample(r.value_c, r.valid);
    }

    /* Hourly flush */
    if (now - s_last_flush_time >= FLUSH_SECS) {
        s_last_flush_time = now;
        flush_ring();
    }

    /* Daily purge */
    if (now - s_last_purge_time >= PURGE_SECS) {
        s_last_purge_time = now;
        purge_old();
    }
}

/* ── Public API ── */

uint32_t history_record_count(void)
{
    return s_record_count;
}

void history_query(uint32_t from, uint32_t to, history_cb_t cb, void *ctx)
{
    DIR *d = opendir(HIST_DIR);
    if (!d) return;

    /* Collect matching filenames into sorted array */
    char names[16][16];
    int  nfiles = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && nfiles < 16) {
        if (strstr(ent->d_name, ".bin"))
            strlcpy(names[nfiles++], ent->d_name, 16);
    }
    closedir(d);

    /* Sort ascending (YYYYMM.bin is lexicographically ordered by date) */
    for (int i = 0; i < nfiles - 1; i++)
        for (int j = i + 1; j < nfiles; j++)
            if (strcmp(names[i], names[j]) > 0) {
                char tmp[16]; memcpy(tmp, names[i], 16);
                memcpy(names[i], names[j], 16); memcpy(names[j], tmp, 16);
            }

    for (int fi = 0; fi < nfiles; fi++) {
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", HIST_DIR, names[fi]);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        hist_record_t rec;
        while (fread(&rec, sizeof(rec), 1, f) == 1) {
            /* Verify CRC — skip torn records */
            if (rec.crc8 != crc8((uint8_t *)&rec, offsetof(hist_record_t, crc8)))
                continue;
            if (!(rec.flags & 0x01)) continue; /* invalid reading */
            if (rec.epoch < from || rec.epoch > to) continue;
            cb(rec.epoch, (float)rec.temp_centi / 100.0f, ctx);
        }
        fclose(f);
    }
}

void history_start(void)
{
    s_ring_mutex = xSemaphoreCreateMutex();
    esp_event_handler_register(APP_EVENT, APP_EVT_READING_UPDATED, on_reading, NULL);
    ESP_LOGI(TAG, "History component started");
}
