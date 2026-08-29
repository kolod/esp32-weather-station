idf_component_register(
    SRCS "sensor.c" "bmp280.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_timer app_ctx esp_driver_i2c
    PRIV_REQUIRES onewire_bus ds18b20 esp_driver_gpio
)
