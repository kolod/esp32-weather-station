idf_component_register(
    SRCS "settings.c" "tz_table.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_event app_ctx
)
