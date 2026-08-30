#pragma once
#include "esp_http_server.h"

/**
 * @brief Live-readings WebSocket broadcaster (feature 009).
 *
 * On any relevant APP_EVENT (reading updated, wifi state, time synced/restored,
 * settings changed) it builds the shared status-JSON snapshot once and pushes it
 * to every connected /api/ws client.
 *
 * Clients are discovered directly from the server (httpd_get_client_list +
 * httpd_ws_get_fd_info) — a browser WebSocket never sends a frame, so the
 * /api/ws handler is not called after the handshake and cannot self-register.
 *
 * The frame send is deferred onto the httpd worker via httpd_queue_work();
 * the event handler only enqueues.
 */

/** @brief Capture the server handle and subscribe to APP_EVENT. Idempotent. */
void ws_broadcast_start(httpd_handle_t server);

/** @brief Unsubscribe. */
void ws_broadcast_stop(void);
