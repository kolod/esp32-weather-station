#include "settings.h"
#include "tz_table.h"
#include "app_ctx.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAG "settings"
#define NVS_NS "settings"

static nvs_handle_t s_nvs;

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
    return err;
}

/* ── Getters ── */

esp_err_t settings_get_tz_name(char *buf, size_t len)
{
    esp_err_t err = nvs_get_str(s_nvs, "tz_name", buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strlcpy(buf, SETTINGS_DEFAULT_TZ_NAME, len);
        return ESP_OK;
    }
    return err;
}

esp_err_t settings_get_tz_posix(char *buf, size_t len)
{
    esp_err_t err = nvs_get_str(s_nvs, "tz_posix", buf, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strlcpy(buf, SETTINGS_DEFAULT_TZ_POSIX, len);
        return ESP_OK;
    }
    return err;
}

uint8_t settings_get_time_mode(void)
{
    uint8_t v = TIME_MODE_LOCAL;
    nvs_get_u8(s_nvs, "time_mode", &v);
    return v;
}

uint8_t settings_get_temp_unit(void)
{
    uint8_t v = TEMP_UNIT_CELSIUS;
    nvs_get_u8(s_nvs, "temp_unit", &v);
    return v;
}

/* ── Setters ── */

esp_err_t settings_set_timezone(const char *iana_name)
{
    const char *posix = tz_find_posix(iana_name);
    if (!posix) {
        ESP_LOGW(TAG, "Unknown timezone: %s", iana_name);
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err;
    err = nvs_set_str(s_nvs, "tz_name", iana_name);
    if (err != ESP_OK) return err;
    err = nvs_set_str(s_nvs, "tz_posix", posix);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_nvs);
    if (err == ESP_OK) {
        settings_apply_timezone();
        app_event_post(APP_EVT_SETTINGS_CHANGED);
    }
    return err;
}

esp_err_t settings_set_time_mode(uint8_t mode)
{
    esp_err_t err = nvs_set_u8(s_nvs, "time_mode", mode);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_nvs);
    if (err == ESP_OK) app_event_post(APP_EVT_SETTINGS_CHANGED);
    return err;
}

esp_err_t settings_set_temp_unit(uint8_t unit)
{
    esp_err_t err = nvs_set_u8(s_nvs, "temp_unit", unit);
    if (err != ESP_OK) return err;
    err = nvs_commit(s_nvs);
    if (err == ESP_OK) app_event_post(APP_EVT_SETTINGS_CHANGED);
    return err;
}

void settings_apply_timezone(void)
{
    char posix[64];
    settings_get_tz_posix(posix, sizeof(posix));
    setenv("TZ", posix, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set: %s", posix);
}
