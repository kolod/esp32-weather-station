#pragma once
#include "esp_http_server.h"

/** @brief Register all management page handlers on the given HTTPS server. */
void register_mgmt_handlers(httpd_handle_t server);
