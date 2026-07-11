#include "buttons.h"
#include "settings.h"
#include "app_ctx.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "iot_button.h"
#include "button_gpio.h"

#define TAG          "buttons"
#define GPIO_LEFT    0   /* active-low, internal pull-up (strap pin — safe after boot) */
#define GPIO_RIGHT   35  /* active-low, input-only — relies on board external pull-up  */

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

    iot_button_register_cb(left,  BUTTON_SINGLE_CLICK,    NULL, on_left_click, NULL);
    iot_button_register_cb(right, BUTTON_SINGLE_CLICK,    NULL, on_right_click, NULL);
    iot_button_register_cb(left,  BUTTON_LONG_PRESS_START, NULL, on_left_long, NULL);
}
