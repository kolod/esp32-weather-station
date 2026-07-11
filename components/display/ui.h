#pragma once
#include "lvgl.h"
#include <time.h>

/** @brief Create and lay out all LVGL widgets on the main screen.
 *         Must be called inside lvgl_port_lock(). */
void ui_init(void);

/** @brief Update the temperature label (°C or °F based on current setting).
 *         Must be called inside lvgl_port_lock(). */
void ui_set_temperature(float value_c, bool valid, uint8_t temp_unit);

/** @brief Update the time label and mode indicator.
 *         Must be called inside lvgl_port_lock(). */
void ui_set_time(bool synced, time_t now, uint8_t time_mode);

/** @brief Update the WiFi status icon.
 *         Must be called inside lvgl_port_lock(). */
void ui_set_wifi_state(int wifi_state);
