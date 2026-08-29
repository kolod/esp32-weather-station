#pragma once
#include "esp_http_server.h"
#include <stdbool.h>

/**
 * @brief Live-readings WebSocket broadcaster (feature 009).
 *
 * Keeps the set of connected /api/ws client sockets and, on any relevant
 * APP_EVENT (reading updated, wifi state, time synced/restored, settings
 * changed), pushes the shared status-JSON snapshot to every client.
 *
 * The actual frame send is deferred onto the httpd worker via
 * httpd_queue_work(); the event handler only enqueues.
 */

/** @brief Capture the server handle and subscribe to APP_EVENT. Idempotent. */
void ws_broadcast_start(httpd_handle_t server);

/** @brief Unsubscribe and drop all tracked clients. */
void ws_broadcast_stop(void);

/**
 * @brief Register a freshly handshaken /api/ws client socket.
 * @return true if accepted; false if the registry is full (caller should
 *         close the connection).
 */
bool ws_broadcast_add_client(int fd);

/** @brief Forget a client socket (on close or send failure). */
void ws_broadcast_remove_client(int fd);
