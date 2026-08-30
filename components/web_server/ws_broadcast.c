#include "ws_broadcast.h"
#include "handlers_mgmt.h"   /* build_status_json() */
#include "app_ctx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ws_broadcast"

/* Upper bound on sockets to enumerate — must cover cfg.httpd.max_open_sockets. */
#define WS_FD_SCAN_MAX 8

/* Status JSON fits well under 1152 bytes (see build_status_json). */
#define WS_JSON_CAP 1200

static httpd_handle_t s_server = NULL;

/* Runs on the httpd worker (via httpd_queue_work): build the snapshot once and
 * push it to every socket that has actually upgraded to a WebSocket.
 *
 * The client set is queried straight from the server (httpd_get_client_list +
 * httpd_ws_get_fd_info) rather than tracked in a handler. A browser WebSocket
 * never sends a frame, so the /api/ws handler is not invoked after the
 * handshake and cannot self-register — the previous registry stayed empty and
 * nothing was ever pushed (feature 009 latent bug; the page fell back to its
 * 5 s poll). */
static void ws_broadcast_work(void *arg)
{
    (void)arg;
    if (!s_server) return;

    char *json = malloc(WS_JSON_CAP);
    if (!json) return;
    if (build_status_json(json, WS_JSON_CAP) != ESP_OK) {
        free(json);
        return;
    }
    size_t json_len = strlen(json);

    int fds[WS_FD_SCAN_MAX];
    size_t n = WS_FD_SCAN_MAX;
    if (httpd_get_client_list(s_server, &n, fds) != ESP_OK) {
        free(json);
        return;
    }

    unsigned sent = 0;
    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;   /* plain HTTP socket — not a live-readings client */
        }
        httpd_ws_frame_t frame = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len     = json_len,
        };
        if (httpd_ws_send_frame_async(s_server, fds[i], &frame) == ESP_OK) sent++;
    }
    ESP_LOGD(TAG, "pushed snapshot to %u WS client(s) of %u socket(s)", sent, (unsigned)n);
    free(json);
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    if (!s_server) return;
    /* Non-blocking on IDF 6.1 (fast-fails if the ctrl mbox is momentarily full);
       the next event will re-enqueue, and readings update at most every 5 s. */
    httpd_queue_work(s_server, ws_broadcast_work, NULL);
}

void ws_broadcast_start(httpd_handle_t server)
{
    if (s_server) return;
    s_server = server;

    esp_event_handler_register(APP_EVENT, APP_EVT_READING_UPDATED,    on_app_event, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_WIFI_STATE_CHANGED, on_app_event, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_TIME_SYNCED,        on_app_event, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_TIME_RESTORED,      on_app_event, NULL);
    esp_event_handler_register(APP_EVENT, APP_EVT_SETTINGS_CHANGED,   on_app_event, NULL);
    ESP_LOGI(TAG, "started");
}

void ws_broadcast_stop(void)
{
    if (!s_server) return;
    esp_event_handler_unregister(APP_EVENT, APP_EVT_READING_UPDATED,    on_app_event);
    esp_event_handler_unregister(APP_EVENT, APP_EVT_WIFI_STATE_CHANGED, on_app_event);
    esp_event_handler_unregister(APP_EVENT, APP_EVT_TIME_SYNCED,        on_app_event);
    esp_event_handler_unregister(APP_EVENT, APP_EVT_TIME_RESTORED,      on_app_event);
    esp_event_handler_unregister(APP_EVENT, APP_EVT_SETTINGS_CHANGED,   on_app_event);
    s_server = NULL;
    ESP_LOGI(TAG, "stopped");
}
