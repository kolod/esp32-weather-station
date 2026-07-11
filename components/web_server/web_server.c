#include "web_server.h"
#include "portal_server.h"
#include "mgmt_server.h"
#include "captive_dns.h"
#include "app_ctx.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "web_server"

static void on_wifi_state(void *arg, esp_event_base_t base,
                           int32_t id, void *data)
{
    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    wifi_state_t ws = app_state.wifi_state;
    xSemaphoreGive(app_state_mutex);

    switch (ws) {
    case WIFI_ST_PROVISIONING_AP:
    case WIFI_ST_AP_FALLBACK:
        mgmt_server_stop();
        portal_server_start();
        captive_dns_start();
        break;
    case WIFI_ST_CONNECTED:
        portal_server_stop();
        captive_dns_stop();
        mgmt_server_start(); /* no-op if cert not provisioned */
        break;
    default:
        break;
    }
}

static void web_server_task(void *arg)
{
    esp_event_handler_register(APP_EVENT, APP_EVT_WIFI_STATE_CHANGED,
                                on_wifi_state, NULL);
    /* Block forever — event-driven only */
    while (true) vTaskDelay(pdMS_TO_TICKS(60000));
}

void web_server_start(void)
{
    xTaskCreate(web_server_task, "web_server", 8192, NULL, 4, NULL);
}
