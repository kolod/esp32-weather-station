#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Time display mode */
#define TIME_MODE_LOCAL 0
#define TIME_MODE_UTC   1

/* Temperature unit */
#define TEMP_UNIT_CELSIUS    0
#define TEMP_UNIT_FAHRENHEIT 1

/* Default timezone (IANA name) */
#define SETTINGS_DEFAULT_TZ_NAME  "UTC"
#define SETTINGS_DEFAULT_TZ_POSIX "UTC0"

/**
 * @brief Initialize the settings component (opens NVS namespace).
 *        Must be called after nvs_flash_init().
 */
esp_err_t settings_init(void);

/* ── Getters ── */
esp_err_t settings_get_tz_name(char *buf, size_t len);
esp_err_t settings_get_tz_posix(char *buf, size_t len);
uint8_t   settings_get_time_mode(void);
uint8_t   settings_get_temp_unit(void);

/* ── Setters (each does a single nvs_commit) ── */
esp_err_t settings_set_timezone(const char *iana_name); /* looks up POSIX, saves both */
esp_err_t settings_set_time_mode(uint8_t mode);
esp_err_t settings_set_temp_unit(uint8_t unit);

/**
 * @brief Apply the stored POSIX timezone string to the C runtime via setenv/tzset.
 *        Call after settings_init() and after every SETTINGS_CHANGED event.
 */
void settings_apply_timezone(void);
