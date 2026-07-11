idf_component_register(
    SRCS "wifi_mgr.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_event esp_netif settings app_ctx
    PRIV_REQUIRES mdns lwip
)
