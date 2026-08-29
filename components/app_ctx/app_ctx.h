#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"

/* ── Temperature reading (written by sensor task, read by display/history/web) ── */
typedef struct {
    float    value_c;       /* sensor reading in °C */
    bool     valid;         /* false: sensor absent, read error, or out of range */
    int64_t  updated_at_ms; /* esp_timer_get_time() / 1000 at last write */
} temperature_reading_t;

/* ── Pressure reading (feature 005: BMP280/BME280; valid in BMP280 & BME280 modes) ── */
typedef struct {
    float    value_hpa;     /* station pressure in hPa */
    bool     valid;         /* false: no Bosch sensor, read error, or out of range */
    int64_t  updated_at_ms; /* esp_timer_get_time() / 1000 at last write */
} pressure_reading_t;

/* ── Humidity reading (feature 007: BME280; valid only in SENSOR_BME280 mode) ── */
typedef struct {
    float    value_pct;     /* relative humidity, 0.0–100.0 %RH */
    bool     valid;         /* false: no BME280, read error, or out of range */
    int64_t  updated_at_ms; /* esp_timer_get_time() / 1000 at last write */
} humidity_reading_t;

/* ── Sensor detected at boot (feature 005/007); fixed until reboot ── */
typedef enum {
    SENSOR_NONE = 0,  /* no sensor responded: all readings stay invalid    */
    SENSOR_DS18B20,   /* wired probe: temperature only (pre-005 behavior)  */
    SENSOR_BMP280,    /* I2C BMP280: temperature and pressure              */
    SENSOR_BME280,    /* I2C BME280: temperature, pressure and humidity    */
} sensor_kind_t;

/* ── WiFi state ── */
typedef enum {
    WIFI_ST_IDLE = 0,
    WIFI_ST_PROVISIONING_AP,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
    WIFI_ST_RETRYING,
    WIFI_ST_AP_FALLBACK,
} wifi_state_t;

/* ── OTA update session state ── */
typedef enum {
    OTA_ST_IDLE = 0,
    OTA_ST_RECEIVING,
    OTA_ST_VALIDATING,
    OTA_ST_APPLIED_PENDING_REBOOT,
    OTA_ST_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    uint8_t     progress_pct;
    char        error[64]; /* human-readable reason on FAILED, empty otherwise */
} ota_session_t;

/* ── Time source (feature 002: RTC time restore) ── */
typedef enum {
    APP_TIME_SOURCE_NONE = 0, /* no trustworthy time; display shows "not available"    */
    APP_TIME_SOURCE_RTC,      /* restored from battery-backed clock, not yet verified  */
    APP_TIME_SOURCE_NTP,      /* network-synchronized this boot                        */
} app_time_source_t;

/* ── Shared application state (guarded by app_state_mutex) ── */
typedef struct {
    temperature_reading_t reading;
    pressure_reading_t    pressure;    /* valid when sensor_kind is SENSOR_BMP280 or SENSOR_BME280 */
    humidity_reading_t    humidity;    /* valid only when sensor_kind == SENSOR_BME280 */
    sensor_kind_t         sensor_kind; /* boot-time detection result (feature 005/007) */
    wifi_state_t          wifi_state;
    ota_session_t         ota;
    bool                  time_synced; /* NTP-synced this boot (time_source == NTP)    */
    app_time_source_t     time_source; /* time_source != NONE ⇒ time is displayable    */
} app_state_t;

extern app_state_t       app_state;
extern SemaphoreHandle_t app_state_mutex;

/* ── Custom event base & IDs ── */
ESP_EVENT_DECLARE_BASE(APP_EVENT);

typedef enum {
    APP_EVT_READING_UPDATED = 0, /* sensor posted a new temperature reading      */
    APP_EVT_TIME_SYNCED,         /* SNTP obtained first time reference            */
    APP_EVT_SETTINGS_CHANGED,    /* one or more NVS settings changed              */
    APP_EVT_WIFI_STATE_CHANGED,  /* wifi_state_t changed; data: new wifi_state_t */
    APP_EVT_OTA_STATE_CHANGED,   /* ota_session_t changed                         */
    APP_EVT_TIME_RESTORED,       /* valid time restored from battery-backed RTC  */
} app_event_id_t;

/* Helper: post an event with no payload */
static inline void app_event_post(app_event_id_t id)
{
    esp_event_post(APP_EVENT, id, NULL, 0, 0);
}
