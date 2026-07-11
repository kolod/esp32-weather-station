idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_system
        app_ctx
        settings
        sensor
        history
        display
        wifi_mgr
        web_server
        littlefs
        app_update
)
