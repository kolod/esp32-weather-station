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
#include "lwip/ip4_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

#define TAG            "wifi_mgr"
#define AP_MAX_CONN    4
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
    /* Step the clock on every sync (the lwIP default).
     *
     * SNTP_SYNC_MODE_SMOOTH is unusable on ESP-IDF 6.1: its "step immediately
     * when the offset is too large to slew" fallback depends on adjtime()
     * returning -1 for a big delta. In 6.1 adjtime() first narrows
     * `delta->tv_sec * 1000000` into the 32-bit `long` of `struct timex.offset`
     * and only range-checks afterwards, so a cold-boot 1970 -> now correction
     * (~1.8e15 us) overflows to a small value, is accepted for slewing, and is
     * never actually applied — SNTP reports "synchronized" while the clock
     * stays at 1970 / shows 00:00. (Worked on 6.0.2, whose adjtime() checked
     * the raw 64-bit seconds before narrowing.)
     *
     * The smooth-slew benefit is negligible here anyway: feature 002's RTC
     * restore already prevents the visible jump at boot, and routine hourly
     * re-sync offsets are sub-second — invisible on an HH:MM display. */
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
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

static void netif_ipv4_str(esp_netif_t *netif, char *buf, size_t len)
{
    esp_netif_ip_info_t ip_info = {};
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
        esp_ip4addr_ntoa(&ip_info.ip, buf, len);
    }
}

void wifi_mgr_get_info(wifi_mgr_info_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    /* Hostname is derived from the MAC and is meaningful in every state, even
       before mDNS starts (it just won't resolve until STA is up). */
    char suffix[5];
    mac_to_suffix(suffix);
    snprintf(out->hostname, sizeof(out->hostname), "weather-%s.local", suffix);

    switch (s_state) {
    case WIFI_ST_CONNECTED: {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            strlcpy(out->ssid, (const char *)ap.ssid, sizeof(out->ssid));
        }
        netif_ipv4_str(s_sta_netif, out->ipv4, sizeof(out->ipv4));
        break;
    }
    case WIFI_ST_PROVISIONING_AP:
    case WIFI_ST_AP_FALLBACK: {
        wifi_config_t ap_cfg = {};
        if (esp_wifi_get_config(WIFI_IF_AP, &ap_cfg) == ESP_OK) {
            strlcpy(out->ssid, (const char *)ap_cfg.ap.ssid, sizeof(out->ssid));
        }
        netif_ipv4_str(s_ap_netif, out->ipv4, sizeof(out->ipv4));
        break;
    }
    case WIFI_ST_CONNECTING:
    case WIFI_ST_RETRYING: {
        wifi_config_t sta_cfg = {};
        if (esp_wifi_get_config(WIFI_IF_STA, &sta_cfg) == ESP_OK) {
            strlcpy(out->ssid, (const char *)sta_cfg.sta.ssid, sizeof(out->ssid));
        }
        break; /* no IP yet — ipv4 stays "" */
    }
    case WIFI_ST_IDLE:
    default:
        break; /* ssid and ipv4 stay "" */
    }
}

void wifi_mgr_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    esp_netif_dhcps_stop(s_ap_netif);
    esp_netif_ip_info_t ap_ip_info = {};
    IP4_ADDR(&ap_ip_info.ip,      WIFI_MGR_AP_IP_1, WIFI_MGR_AP_IP_2, WIFI_MGR_AP_IP_3, WIFI_MGR_AP_IP_4);
    IP4_ADDR(&ap_ip_info.gw,      WIFI_MGR_AP_IP_1, WIFI_MGR_AP_IP_2, WIFI_MGR_AP_IP_3, WIFI_MGR_AP_IP_4);
    IP4_ADDR(&ap_ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_ap_netif, &ap_ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(s_ap_netif));

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
