#pragma once
#include "esp_http_server.h"

/** @brief Register POST /api/ota and GET /api/ota/status on the given server. */
void register_ota_handlers(httpd_handle_t server);
