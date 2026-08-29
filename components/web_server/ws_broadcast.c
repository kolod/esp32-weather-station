#include "ws_broadcast.h"
#include "handlers_mgmt.h"   /* build_status_json() */
#include "app_ctx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>

#define TAG "ws_broadcast"

/* One live WebSocket permanently occupies a server socket; keep this below
   cfg.httpd.max_open_sockets (5) so a transient request can still be served.
   spec 009: ~4 concurrent viewers on a home LAN. */
#define WS_MAX_CLIENTS 4

/* Status JSON fits well under 1152 bytes (see build_status_json). */
#define WS_JSON_CAP 1200

static httpd_handle_t    s_server = NULL;
static int               s_fds[WS_MAX_CLIENTS];
static size_t            s_count = 0;
static SemaphoreHandle_t s_lock  = NULL;

static inline void lock(void)   { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); }
static inline void unlock(void) { if (s_lock) xSemaphoreGive(s_lock); }

bool ws_broadcast_add_client(int fd)
{
    bool ok = false;
    lock();
    for (size_t i = 0; i < s_count; i++) {
        if (s_fds[i] == fd) { ok = true; goto out; } /* already tracked */
    }
    if (s_count < WS_MAX_CLIENTS) {
        s_fds[s_count++] = fd;
        ok = true;
    }
out:
    unlock();
    ESP_LOGI(TAG, "add client fd=%d -> %s (%u total)", fd,
             ok ? "ok" : "rejected (full)", (unsigned)s_count);
    return ok;
}

void ws_broadcast_remove_client(int fd)
{
    lock();
    for (size_t i = 0; i < s_count; i++) {
        if (s_fds[i] == fd) {
            s_fds[i] = s_fds[--s_count];
            ESP_LOGI(TAG, "remove client fd=%d (%u total)", fd, (unsigned)s_count);
            break;
        }
    }
    unlock();
}

/* Runs on the httpd worker (via httpd_queue_work): build the snapshot once and
   send it to every tracked client, pruning any that fail. */
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

    int fds[WS_MAX_CLIENTS];
    size_t n;
    lock();
    n = s_count;
    memcpy(fds, s_fds, n * sizeof(int));
    unlock();

    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            ws_broadcast_remove_client(fds[i]);
            continue;
        }
        httpd_ws_frame_t frame = {
            .type    = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len     = json_len,
        };
        if (httpd_ws_send_frame_async(s_server, fds[i], &frame) != ESP_OK) {
            ws_broadcast_remove_client(fds[i]);
        }
    }
    free(json);
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    if (!s_server) return;
    lock();
    size_t n = s_count;
    unlock();
    if (n == 0) return;
    httpd_queue_work(s_server, ws_broadcast_work, NULL);
}

void ws_broadcast_start(httpd_handle_t server)
{
    if (s_server) return;
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    s_server = server;
    s_count  = 0;

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
    lock();
    s_count  = 0;
    s_server = NULL;
    unlock();
    ESP_LOGI(TAG, "stopped");
}
