idf_component_register(
    SRCS
        "web_server.c"
        "portal_server.c"
        "mgmt_server.c"
        "i18n.c"
        "handlers_common.c"
        "handlers_mgmt.c"
        "handlers_ota.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_https_server esp_event settings history
             app_ctx captive_dns wifi_mgr esp_wifi esp_netif app_update esp_timer vfs
    EMBED_FILES
        "www/portal/index.html"
        "www/portal/portal.css"
        "www/portal/portal.js"
        "www/i18n/en.json"
        "www/i18n/de.json"
        "www/i18n/fr.json"
        "www/i18n/uk.json"
        "www/mgmt/mgmt.html"
        "www/mgmt/mgmt.css"
        "www/mgmt/mgmt.js"
)
