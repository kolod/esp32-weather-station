idf_component_register(
    SRCS "display.c" "ui.c" "buttons.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event settings app_ctx esp_wifi nvs_flash
    PRIV_REQUIRES esp_lcd esp_lvgl_port button esp_timer esp_driver_ledc esp_driver_spi
)
