#include "display.h"
#include "ui.h"
#include "buttons.h"
#include "app_ctx.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lvgl_port.h"

#define TAG "display"

/* ── Pin assignments (from spec/plan) ── */
#define LCD_MOSI   19
#define LCD_SCLK   18
#define LCD_CS      5
#define LCD_DC     16
#define LCD_RST    23
#define LCD_BL      4

/* ── Panel resolution (Kconfig-overridable; verify gap offsets on first power-on) ── */
#ifndef LCD_H_RES
#define LCD_H_RES 250
#endif
#ifndef LCD_V_RES
#define LCD_V_RES 135
#endif
/* T-Display gap offsets for ST7789V (common values; adjust if pixels are shifted) */
#ifndef LCD_X_GAP
#define LCD_X_GAP 40
#endif
#ifndef LCD_Y_GAP
#define LCD_Y_GAP 53
#endif

#define LCD_SPI_CLK_HZ (40 * 1000 * 1000)
/* Partial buffer: 1/8 of vertical resolution to keep within internal SRAM */
#define LCD_DRAW_LINES (LCD_V_RES / 8)

static lv_display_t *s_disp = NULL;

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1023, /* 100% */
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

static void panel_init(void)
{
    /* SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num   = LCD_MOSI,
        .miso_io_num   = -1,
        .sclk_io_num   = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_LINES * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* Panel IO */
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = LCD_DC,
        .cs_gpio_num       = LCD_CS,
        .pclk_hz           = LCD_SPI_CLK_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                              &io_cfg, &io_handle));

    /* ST7789V panel driver */
    esp_lcd_panel_handle_t panel_handle;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_cfg, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); /* T-Display needs inversion */
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_X_GAP, LCD_Y_GAP));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    /* esp_lvgl_port */
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 6144;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = io_handle,
        .panel_handle  = panel_handle,
        .buffer_size   = LCD_H_RES * LCD_DRAW_LINES,
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .color_format  = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy  = true,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    configASSERT(s_disp);
}

static void display_event_handler(void *arg, esp_event_base_t base,
                                   int32_t id, void *data)
{
    if (!lvgl_port_lock(pdMS_TO_TICKS(100))) return;

    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    temperature_reading_t r = app_state.reading;
    bool time_synced         = app_state.time_synced;
    wifi_state_t ws          = app_state.wifi_state;
    xSemaphoreGive(app_state_mutex);

    uint8_t unit = settings_get_temp_unit();
    uint8_t mode = settings_get_time_mode();

    ui_set_temperature(r.value_c, r.valid, unit);
    ui_set_time(time_synced, time(NULL), mode);
    ui_set_wifi_state((int)ws);

    lvgl_port_unlock();
}

static void display_task(void *arg)
{
    panel_init();
    backlight_init();
    buttons_init();

    lvgl_port_lock(portMAX_DELAY);
    ui_init();
    lvgl_port_unlock();

    /* Subscribe to all relevant app events */
    esp_event_handler_register(APP_EVENT, APP_EVT_READING_UPDATED,
                                display_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_TIME_SYNCED,
                                display_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_SETTINGS_CHANGED,
                                display_event_handler, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_WIFI_STATE_CHANGED,
                                display_event_handler, NULL);

    /* Initial render */
    display_event_handler(NULL, NULL, 0, NULL);

    /* Clock refresh every minute while task is running */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        display_event_handler(NULL, NULL, 0, NULL);
    }
}

void display_start(void)
{
    xTaskCreate(display_task, "display", 6144, NULL, 4, NULL);
}
