#pragma once
#include "esp_http_server.h"

/** @brief Register GET /api/timezones on the given server handle. */
void register_timezones_handler(httpd_handle_t server);
