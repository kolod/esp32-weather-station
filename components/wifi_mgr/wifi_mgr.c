#include "wifi_mgr.h"
#include "app_ctx.h"
#include "settings.h"
#include "rtc_time.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "apps/esp_sntp.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define TAG            "wifi_mgr"
#define AP_MAX_CONN    4
#define AP_IP_ADDR     "192.168.4.1"
#define MAX_RETRIES    6   /* ~63 s total with exponential back-off */
#define RETRY_BASE_MS  1000

static int            s_retry_count = 0;
static wifi_state_t   s_state       = WIFI_ST_IDLE;
static esp_netif_t   *s_sta_netif   = NULL;
static esp_netif_t   *s_ap_netif    = NULL;

void mac_to_suffix(char *buf)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buf, 5, "%02x%02x", mac[4], mac[5]);
}

static void set_state(wifi_state_t st)
{
    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    app_state.wifi_state = st;
    xSemaphoreGive(app_state_mutex);
    s_state = st;
    app_event_post(APP_EVT_WIFI_STATE_CHANGED);
}

static void start_ap(void)
{
    char suffix[5];
    mac_to_suffix(suffix);
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "weather-%s", suffix);

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid_len       = 0,
            .channel        = 6,
            .authmode       = WIFI_AUTH_OPEN,
            .max_connection = AP_MAX_CONN,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started: SSID=%s", ssid);
    set_state(WIFI_ST_PROVISIONING_AP);
}

static void stop_ap(void)
{
    wifi_config_t ap_cfg = {};
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    /* Switch to STA-only once connected */
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "AP stopped");
}

static void start_sntp(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    /* Slew small post-restore offsets instead of stepping, so the displayed
       time never visibly jumps; offsets beyond the lwIP threshold still step */
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
}

static void start_mdns(void)
{
    char suffix[5];
    mac_to_suffix(suffix);
    char hostname[32];
    snprintf(hostname, sizeof(hostname), "weather-%s", suffix);

    mdns_init();
    mdns_hostname_set(hostname);
    mdns_instance_name_set("ESP32 Weather Station");
    ESP_LOGI(TAG, "mDNS: %s.local", hostname);
}

static void sntp_sync_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP synchronized");
    settings_apply_timezone();
    rtc_time_mark_synced(); /* refresh battery-backed validity record; time_source → NTP */
    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    app_state.time_synced = true;
    xSemaphoreGive(app_state_mutex);
    app_event_post(APP_EVT_TIME_SYNCED);
}

static void on_sta_connected(void *arg, esp_event_base_t base,
                              int32_t id, void *data)
{
    ESP_LOGI(TAG, "STA got IP");
    s_retry_count = 0;
    stop_ap();
    start_sntp();
    start_mdns();
    set_state(WIFI_ST_CONNECTED);
}

static void on_sta_disconnected(void *arg, esp_event_base_t base,
                                 int32_t id, void *data)
{
    if (s_state == WIFI_ST_CONNECTED) {
        ESP_LOGW(TAG, "WiFi connection lost, retrying...");
        s_retry_count = 0;
    }
    if (s_retry_count < MAX_RETRIES) {
        uint32_t delay_ms = RETRY_BASE_MS << s_retry_count; /* 1→2→4→8→16→32 s */
        s_retry_count++;
        set_state(WIFI_ST_RETRYING);
        ESP_LOGI(TAG, "Retry %d/%d in %lu ms", s_retry_count, MAX_RETRIES,
                 (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    } else {
        ESP_LOGW(TAG, "Max retries reached — starting AP fallback");
        s_retry_count = 0;
        start_ap();
    }
}

static void try_sta_connect(void)
{
    wifi_config_t sta_cfg = {};
    esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);
    if (strlen((char *)sta_cfg.sta.ssid) == 0) {
        ESP_LOGI(TAG, "No credentials stored — going to AP mode");
        start_ap();
        return;
    }
    set_state(WIFI_ST_CONNECTING);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    ESP_LOGI(TAG, "Connecting to SSID: %s", sta_cfg.sta.ssid);
}

void wifi_mgr_connect_sta(void)
{
    s_retry_count = 0;
    esp_wifi_connect();
}

wifi_state_t wifi_mgr_get_state(void)
{
    return s_state;
}

void wifi_mgr_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                on_sta_connected, NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                on_sta_disconnected, NULL);

    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);

    try_sta_connect();
}
