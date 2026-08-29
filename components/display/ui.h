#pragma once
#include "lvgl.h"
#include "wifi_mgr.h"
#include <time.h>

/**
 * @brief Create and lay out all LVGL widgets on the main screen.
 *
 * Fixed four-quadrant layout on the 250×135 landscape panel
 * (feature 007, contracts/screen-layout.md):
 *   - top-left     : time  (HH:MM) + LOCAL/UTC badge
 *   - top-right    : temperature   + °C/°F badge
 *   - bottom-left  : pressure      + "hPa"
 *   - bottom-right : humidity      + "%"
 *   - centre       : WiFi status glyph
 *
 * Quadrant positions never change with the fitted sensor; a reading that is
 * not available shows a dashed placeholder, never a hidden quadrant.
 *
 * Must be called inside lvgl_port_lock().
 */
void ui_init(void);

/** @brief Update the temperature quadrant (°C or °F based on current setting).
 *         Must be called inside lvgl_port_lock(). */
void ui_set_temperature(float value_c, bool valid, uint8_t temp_unit);

/** @brief Update the pressure quadrant (feature 005).
 *         valid=false renders "---" (with the "hPa" sub-label kept).
 *         Must be called inside lvgl_port_lock(). */
void ui_set_pressure(float value_hpa, bool valid);

/** @brief Update the humidity quadrant (feature 007).
 *         valid=false renders "---" (with the "%" sub-label kept).
 *         Must be called inside lvgl_port_lock(). */
void ui_set_humidity(float value_pct, bool valid);

/** @brief Update the time quadrant and its LOCAL/UTC badge.
 *         Must be called inside lvgl_port_lock(). */
void ui_set_time(bool synced, time_t now, uint8_t time_mode);

/** @brief Update the centred WiFi status glyph colour.
 *         Must be called inside lvgl_port_lock(). */
void ui_set_wifi_state(int wifi_state);

/** @brief Show the full-screen WiFi details overlay (network name, IPv4, local
 *         hostname), replacing the weather quadrants until hidden.
 *         Empty fields in *info render as "---". NULL is ignored.
 *         Caller MUST hold lvgl_port_lock(). Idempotent (re-show refreshes text). */
void ui_show_wifi_details(const wifi_mgr_info_t *info);

/** @brief Hide the WiFi details overlay, revealing the weather quadrants.
 *         No-op if already hidden. Caller MUST hold lvgl_port_lock(). */
void ui_hide_wifi_details(void);
