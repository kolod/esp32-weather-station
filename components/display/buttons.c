#include "buttons.h"
#include "ui.h"
#include "settings.h"
#include "app_ctx.h"
#include "wifi_mgr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "esp_lvgl_port.h"

#define TAG               "buttons"
#define GPIO_LEFT         0   /* active-low, internal pull-up (strap pin — safe after boot) */
#define GPIO_RIGHT        35  /* active-low, input-only — relies on board external pull-up  */
#define WIFI_OVERLAY_US   (10 * 1000 * 1000)  /* auto-return timeout for the WiFi details screen */

/* WiFi details overlay state — read/written only under lvgl_port_lock() so the
   button task and the esp_timer task cannot interleave a show with a hide. */
static bool               s_wifi_overlay_shown = false;
static esp_timer_handle_t s_auto_hide_timer    = NULL;

static void on_left_click(void *handle, void *usr_data)
{
    uint8_t mode = settings_get_time_mode();
    settings_set_time_mode(mode == TIME_MODE_LOCAL ? TIME_MODE_UTC : TIME_MODE_LOCAL);
    ESP_LOGI(TAG, "Time mode → %s", mode == TIME_MODE_LOCAL ? "UTC" : "LOCAL");
}

static void on_right_click(void *handle, void *usr_data)
{
    uint8_t unit = settings_get_temp_unit();
    settings_set_temp_unit(unit == TEMP_UNIT_CELSIUS ? TEMP_UNIT_FAHRENHEIT : TEMP_UNIT_CELSIUS);
    ESP_LOGI(TAG, "Temp unit → %s", unit == TEMP_UNIT_CELSIUS ? "°F" : "°C");
}

static void on_left_long(void *handle, void *usr_data)
{
    ESP_LOGW(TAG, "Factory reset triggered by long-press!");
    esp_wifi_restore();
    nvs_flash_erase();
    esp_restart();
}

/* esp_timer task context — return the display to the weather view. */
static void auto_hide_cb(void *arg)
{
    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) return;
    ui_hide_wifi_details();
    s_wifi_overlay_shown = false;
    lvgl_port_unlock();
}

static void on_right_long(void *handle, void *usr_data)
{
    /* Snapshot WiFi facts before taking the LVGL lock (non-blocking, but keeps
       the lock hold short). */
    wifi_mgr_info_t info;
    wifi_mgr_get_info(&info);

    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) return;
    if (s_wifi_overlay_shown) {
        ui_hide_wifi_details();
        s_wifi_overlay_shown = false;
        esp_timer_stop(s_auto_hide_timer);
    } else {
        ui_show_wifi_details(&info);
        s_wifi_overlay_shown = true;
        esp_timer_stop(s_auto_hide_timer);
        esp_timer_start_once(s_auto_hide_timer, WIFI_OVERLAY_US);
        ESP_LOGI(TAG, "WiFi details: ssid='%s' ip='%s' host='%s'",
                 info.ssid, info.ipv4, info.hostname);
    }
    lvgl_port_unlock();
}

void buttons_init(void)
{
    button_config_t btn_cfg = {0};  /* default long/short press times */

    button_gpio_config_t left_gpio = {
        .gpio_num     = GPIO_LEFT,
        .active_level = 0,
    };
    button_handle_t left = NULL;
    iot_button_new_gpio_device(&btn_cfg, &left_gpio, &left);

    button_gpio_config_t right_gpio = {
        .gpio_num     = GPIO_RIGHT,
        .active_level = 0,
        .disable_pull = true,  /* GPIO35 is input-only; board has external pull-up */
    };
    button_handle_t right = NULL;
    iot_button_new_gpio_device(&btn_cfg, &right_gpio, &right);

    const esp_timer_create_args_t timer_args = {
        .callback        = auto_hide_cb,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "wifi_overlay",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_auto_hide_timer));

    iot_button_register_cb(left,  BUTTON_SINGLE_CLICK,     NULL, on_left_click, NULL);
    iot_button_register_cb(right, BUTTON_SINGLE_CLICK,     NULL, on_right_click, NULL);
    iot_button_register_cb(left,  BUTTON_LONG_PRESS_START, NULL, on_left_long, NULL);
    iot_button_register_cb(right, BUTTON_LONG_PRESS_START, NULL, on_right_long, NULL);
}
