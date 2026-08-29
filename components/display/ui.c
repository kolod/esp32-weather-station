#include "ui.h"
#include "app_ctx.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

#define TAG "ui"

/* Panel is 250×135 landscape; four ~125×67 quadrants with a 1 px midline
   gutter so the centred WiFi glyph never touches quadrant text
   (contracts/screen-layout.md).

   Exactly two type sizes: FONT_VALUE for every quadrant reading (time,
   temperature, pressure, humidity) and FONT_LABEL for every label
   (LOCAL/UTC, °C/°F, hPa, %). Both must be enabled in sdkconfig.defaults. */
#define QUAD_W       125
#define QUAD_H       67
#define ROW2_Y       68
#define FONT_VALUE   (&lv_font_montserrat_28)
#define FONT_LABEL   (&lv_font_montserrat_14)

/* Widget handles — valid after ui_init() */
static lv_obj_t *s_lbl_time    = NULL;
static lv_obj_t *s_lbl_mode    = NULL;
static lv_obj_t *s_lbl_temp    = NULL;
static lv_obj_t *s_lbl_unit    = NULL;
static lv_obj_t *s_lbl_press   = NULL;
static lv_obj_t *s_lbl_hum     = NULL;
static lv_obj_t *s_lbl_wifi    = NULL;

/* WiFi details overlay (feature 008) — created hidden in ui_init(), toggled by
   ui_show_wifi_details()/ui_hide_wifi_details(). Opaque, covers the quadrants. */
static lv_obj_t *s_wifi_overlay   = NULL;
static lv_obj_t *s_wifi_val_ssid  = NULL;
static lv_obj_t *s_wifi_val_ip    = NULL;
static lv_obj_t *s_wifi_val_host  = NULL;

/* Inline °C→°F conversion (render-time only; storage is always °C) */
static inline float to_fahrenheit(float c) { return c * 9.0f / 5.0f + 32.0f; }

/* Transparent, non-scrolling quadrant container that centre-stacks its
   children (value label on top, unit/badge sub-label below). */
static lv_obj_t *make_quadrant(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *q = lv_obj_create(parent);
    lv_obj_remove_style_all(q);
    lv_obj_set_size(q, QUAD_W, QUAD_H);
    lv_obj_set_pos(q, x, y);
    lv_obj_remove_flag(q, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(q, 0, 0);
    lv_obj_set_flex_flow(q, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(q, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return q;
}

static lv_obj_t *make_value(lv_obj_t *q, const lv_font_t *font, const char *init)
{
    lv_obj_t *l = lv_label_create(q);
    lv_label_set_text(l, init);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t *make_sub(lv_obj_t *q, const char *init)
{
    lv_obj_t *l = lv_label_create(q);
    lv_label_set_text(l, init);
    lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(l, FONT_LABEL, 0);
    return l;
}

/* One "Caption / value" pair inside the WiFi details overlay. Returns the value
   label (grey caption is static). Value uses LONG_DOTS so a 32-char SSID clips
   with an ellipsis instead of overflowing the panel (feature 008, FR-011). */
static lv_obj_t *make_wifi_row(lv_obj_t *parent, const char *caption)
{
    lv_obj_t *cap = lv_label_create(parent);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(cap, FONT_LABEL, 0);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, "---");
    lv_obj_set_style_text_color(val, lv_color_white(), 0);
    lv_obj_set_style_text_font(val, FONT_LABEL, 0);
    lv_obj_set_width(val, LV_PCT(100));
    lv_label_set_long_mode(val, LV_LABEL_LONG_MODE_DOTS);
    return val;
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* Top-left — time */
    lv_obj_t *q_tl = make_quadrant(scr, 0, 0);
    s_lbl_time = make_value(q_tl, FONT_VALUE, "--:--");
    s_lbl_mode = make_sub(q_tl, "LOCAL");

    /* Top-right — temperature */
    lv_obj_t *q_tr = make_quadrant(scr, QUAD_W, 0);
    s_lbl_temp = make_value(q_tr, FONT_VALUE, "---");
    s_lbl_unit = make_sub(q_tr, "°C");

    /* Bottom-left — pressure */
    lv_obj_t *q_bl = make_quadrant(scr, 0, ROW2_Y);
    s_lbl_press = make_value(q_bl, FONT_VALUE, "---");
    make_sub(q_bl, "hPa");

    /* Bottom-right — humidity */
    lv_obj_t *q_br = make_quadrant(scr, QUAD_W, ROW2_Y);
    s_lbl_hum = make_value(q_br, FONT_VALUE, "---");
    make_sub(q_br, "%");

    /* Centre — WiFi status glyph */
    s_lbl_wifi = lv_label_create(scr);
    lv_label_set_text(s_lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_lbl_wifi, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align(s_lbl_wifi, LV_ALIGN_CENTER, 0, 0);

    /* WiFi details overlay — opaque, full-panel, hidden until a right long-press */
    s_wifi_overlay = lv_obj_create(scr);
    lv_obj_remove_style_all(s_wifi_overlay);
    lv_obj_set_size(s_wifi_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(s_wifi_overlay, 0, 0);
    lv_obj_remove_flag(s_wifi_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_wifi_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_wifi_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_wifi_overlay, 6, 0);
    lv_obj_set_style_pad_ver(s_wifi_overlay, 4, 0);
    lv_obj_set_flex_flow(s_wifi_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_wifi_overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    s_wifi_val_ssid = make_wifi_row(s_wifi_overlay, "Network");
    s_wifi_val_ip   = make_wifi_row(s_wifi_overlay, "IP");
    s_wifi_val_host = make_wifi_row(s_wifi_overlay, "Host");
    lv_obj_add_flag(s_wifi_overlay, LV_OBJ_FLAG_HIDDEN);
}

/* "" → "---" so an unknown field never renders as blank space (feature 008, FR-006) */
static void wifi_set_val(lv_obj_t *label, const char *text)
{
    if (!label) return;
    lv_label_set_text(label, (text && text[0]) ? text : "---");
}

void ui_show_wifi_details(const wifi_mgr_info_t *info)
{
    if (!s_wifi_overlay || !info) return;
    wifi_set_val(s_wifi_val_ssid, info->ssid);
    wifi_set_val(s_wifi_val_ip,   info->ipv4);
    wifi_set_val(s_wifi_val_host, info->hostname);
    lv_obj_remove_flag(s_wifi_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_hide_wifi_details(void)
{
    if (!s_wifi_overlay) return;
    lv_obj_add_flag(s_wifi_overlay, LV_OBJ_FLAG_HIDDEN);
}

void ui_set_temperature(float value_c, bool valid, uint8_t temp_unit)
{
    if (!s_lbl_temp) return;
    bool fahrenheit = (temp_unit == TEMP_UNIT_FAHRENHEIT);
    if (!valid) {
        lv_label_set_text(s_lbl_temp, "---");
    } else {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f",
                 fahrenheit ? to_fahrenheit(value_c) : value_c);
        lv_label_set_text(s_lbl_temp, buf);
    }
    lv_label_set_text(s_lbl_unit, fahrenheit ? "°F" : "°C");
}

void ui_set_pressure(float value_hpa, bool valid)
{
    if (!s_lbl_press) return;
    if (valid) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", (double)value_hpa);
        lv_label_set_text(s_lbl_press, buf);
    } else {
        lv_label_set_text(s_lbl_press, "---");
    }
}

void ui_set_humidity(float value_pct, bool valid)
{
    if (!s_lbl_hum) return;
    if (valid) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f", (double)value_pct);
        lv_label_set_text(s_lbl_hum, buf);
    } else {
        lv_label_set_text(s_lbl_hum, "---");
    }
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
    lv_color_t color = (wifi_state == WIFI_ST_CONNECTED)
        ? lv_palette_main(LV_PALETTE_LIGHT_BLUE)
        : lv_palette_main(LV_PALETTE_GREY);
    lv_obj_set_style_text_color(s_lbl_wifi, color, 0);
}
