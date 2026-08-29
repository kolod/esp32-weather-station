#pragma once
#include "esp_http_server.h"
#include "esp_err.h"
#include <stddef.h>

/** @brief Register all management page handlers on the given HTTPS server. */
void register_mgmt_handlers(httpd_handle_t server);

/**
 * @brief Build the management status snapshot as a flat JSON object.
 *
 * Single source of truth for the payload returned by GET /api/status and
 * pushed over the /api/ws WebSocket (feature 009). Writes a NUL-terminated
 * string into @p buf. The full document fits well under 1152 bytes.
 *
 * @param buf      destination buffer.
 * @param buf_len  size of @p buf (>= 1152 recommended).
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if @p buf_len is too small.
 */
esp_err_t build_status_json(char *buf, size_t buf_len);
