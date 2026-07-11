#include "ui.h"
#include "app_ctx.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

#define TAG "ui"

/* Widget handles — valid after ui_init() */
static lv_obj_t *s_lbl_temp   = NULL;
static lv_obj_t *s_lbl_unit   = NULL;
static lv_obj_t *s_lbl_time   = NULL;
static lv_obj_t *s_lbl_mode   = NULL;
static lv_obj_t *s_lbl_wifi   = NULL;

/* Inline °C→°F conversion (render-time only; storage is always °C) */
static inline float to_fahrenheit(float c) { return c * 9.0f / 5.0f + 32.0f; }

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    /* Temperature — large, centered top half */
    s_lbl_temp = lv_label_create(scr);
    lv_label_set_text(s_lbl_temp, "---");
    lv_obj_set_style_text_color(s_lbl_temp, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_lbl_temp, &lv_font_montserrat_48, 0);
    lv_obj_align(s_lbl_temp, LV_ALIGN_TOP_MID, 0, 10);

    /* Unit label (°C / °F) — right of temperature area */
    s_lbl_unit = lv_label_create(scr);
    lv_label_set_text(s_lbl_unit, "°C");
    lv_obj_set_style_text_color(s_lbl_unit, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_lbl_unit, &lv_font_montserrat_20, 0);
    lv_obj_align_to(s_lbl_unit, s_lbl_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 0);

    /* Time — centered bottom half */
    s_lbl_time = lv_label_create(scr);
    lv_label_set_text(s_lbl_time, "--:--");
    lv_obj_set_style_text_color(s_lbl_time, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_lbl_time, &lv_font_montserrat_36, 0);
    lv_obj_align(s_lbl_time, LV_ALIGN_BOTTOM_MID, 0, -22);

    /* Time mode indicator (LOCAL / UTC) */
    s_lbl_mode = lv_label_create(scr);
    lv_label_set_text(s_lbl_mode, "LOCAL");
    lv_obj_set_style_text_color(s_lbl_mode, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(s_lbl_mode, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_mode, LV_ALIGN_BOTTOM_MID, 0, -6);

    /* WiFi indicator — top-right corner */
    s_lbl_wifi = lv_label_create(scr);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_TOP_RIGHT, -4, 4);
}

void ui_set_temperature(float value_c, bool valid, uint8_t temp_unit)
{
    if (!s_lbl_temp) return;
    char buf[16];
    if (!valid) {
        snprintf(buf, sizeof(buf), "---");
    } else if (temp_unit == TEMP_UNIT_FAHRENHEIT) {
        snprintf(buf, sizeof(buf), "%.1f", to_fahrenheit(value_c));
    } else {
        snprintf(buf, sizeof(buf), "%.1f", value_c);
    }
    lv_label_set_text(s_lbl_temp, buf);
    lv_label_set_text(s_lbl_unit, valid ? (temp_unit == TEMP_UNIT_FAHRENHEIT ? "°F" : "°C") : "");
}

void ui_set_time(bool synced, time_t now, uint8_t time_mode)
{
    if (!s_lbl_time) return;
    char tbuf[16];
    if (!synced) {
        snprintf(tbuf, sizeof(tbuf), "--:--");
    } else {
        struct tm tm_info;
        if (time_mode == TIME_MODE_UTC) {
            gmtime_r(&now, &tm_info);
        } else {
            localtime_r(&now, &tm_info);
        }
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", tm_info.tm_hour, tm_info.tm_min);
    }
    lv_label_set_text(s_lbl_time, tbuf);
    lv_label_set_text(s_lbl_mode, time_mode == TIME_MODE_UTC ? "UTC" : "LOCAL");
}

void ui_set_wifi_state(int wifi_state)
{
    if (!s_lbl_wifi) return;
    /* WIFI_ST_CONNECTED = 3 (from app_ctx.h enum) */
    lv_color_t color = (wifi_state == WIFI_ST_CONNECTED)
        ? lv_palette_main(LV_PALETTE_LIGHT_BLUE)
        : lv_palette_main(LV_PALETTE_GREY);
    lv_obj_set_style_text_color(s_lbl_wifi, color, 0);
}
