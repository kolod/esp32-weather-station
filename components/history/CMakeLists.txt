idf_component_register(
    SRCS "history.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_timer littlefs app_ctx settings
)
