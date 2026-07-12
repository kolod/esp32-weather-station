#include "handlers_mgmt.h"
#include "handlers_common.h"
#include "handlers_ota.h"
#include "app_ctx.h"
#include "settings.h"
#include "history.h"
#include "rtc_time.h"
#include "tz_table.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "sys/statvfs.h"
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "handlers_mgmt"

extern const uint8_t mgmt_html_gz_start[] asm("_binary_mgmt_html_start");
extern const uint8_t mgmt_html_gz_end[]   asm("_binary_mgmt_html_end");
extern const uint8_t mgmt_css_gz_start[]  asm("_binary_mgmt_css_start");
extern const uint8_t mgmt_css_gz_end[]    asm("_binary_mgmt_css_end");
extern const uint8_t mgmt_js_gz_start[]   asm("_binary_mgmt_js_start");
extern const uint8_t mgmt_js_gz_end[]     asm("_binary_mgmt_js_end");

/* Extract a plain string value from flat JSON: {"key":"value",...} */
static bool json_str(const char *json, const char *key, char *out, size_t len)
{
    char needle[68];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < len - 1) {
        if (*p == '\\' && *(p + 1)) p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return (*p == '"');
}

/* ── Static assets ── */
static esp_err_t mgmt_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Strict-Transport-Security", "max-age=31536000");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "upgrade-insecure-requests; default-src 'self'");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_send(req, (const char *)mgmt_html_gz_start,
                    mgmt_html_gz_end - mgmt_html_gz_start);
    return ESP_OK;
}
static esp_err_t mgmt_css(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)mgmt_css_gz_start,
                    mgmt_css_gz_end - mgmt_css_gz_start);
    return ESP_OK;
}
static esp_err_t mgmt_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)mgmt_js_gz_start,
                    mgmt_js_gz_end - mgmt_js_gz_start);
    return ESP_OK;
}

/* ── GET /api/status ── */
static esp_err_t api_status(httpd_req_t *req)
{
    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    temperature_reading_t reading = app_state.reading;
    wifi_state_t ws               = app_state.wifi_state;
    bool time_synced              = app_state.time_synced;
    app_time_source_t tsrc        = app_state.time_source;
    xSemaphoreGive(app_state_mutex);

    /* Time-source state + last recorded network sync (feature 002, FR-007) */
    static const char *tsrc_str[] = {"none", "rtc", "ntp"};
    int tsrc_idx = ((int)tsrc >= 0 && (int)tsrc < 3) ? (int)tsrc : 0;
    char last_sync_str[24];
    int64_t last_sync = rtc_time_last_sync();
    if (last_sync < 0)
        strlcpy(last_sync_str, "null", sizeof(last_sync_str));
    else
        snprintf(last_sync_str, sizeof(last_sync_str), "%lld", (long long)last_sync);

    char tz_name[64], time_mode_str[8], temp_unit_str[4];
    settings_get_tz_name(tz_name, sizeof(tz_name));
    uint8_t mode = settings_get_time_mode();
    uint8_t unit = settings_get_temp_unit();
    strlcpy(time_mode_str, mode == TIME_MODE_UTC ? "utc" : "local", sizeof(time_mode_str));
    strlcpy(temp_unit_str, unit == TEMP_UNIT_FAHRENHEIT ? "F" : "C", sizeof(temp_unit_str));

    const esp_app_desc_t *desc = esp_app_get_description();

    /* LittleFS free space */
    struct statvfs st;
    uint32_t free_kb = 0;
    if (statvfs("/storage", &st) == 0)
        free_kb = (uint32_t)(st.f_bavail * st.f_frsize / 1024);

    time_t now = time(NULL);
    uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    const char *wifi_state_str[] = {"idle","provisioning_ap","connecting",
                                     "connected","retrying","ap_fallback"};

    wifi_ap_record_t ap_info = {};
    int rssi = 0;
    char sta_ssid[33] = "";
    if (ws == WIFI_ST_CONNECTED) {
        esp_wifi_sta_get_ap_info(&ap_info);
        rssi = ap_info.rssi;
        strlcpy(sta_ssid, (char *)ap_info.ssid, sizeof(sta_ssid));
    }

    char ip_str[16] = "";
    esp_netif_ip_info_t ip_info;
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta && esp_netif_get_ip_info(sta, &ip_info) == ESP_OK)
        esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str));

    /* Build JSON manually to avoid cJSON dependency */
    char buf[896];
    int ws_idx = ((int)ws >= 0 && (int)ws < 6) ? (int)ws : 0;
    snprintf(buf, sizeof(buf),
        "{"
        "\"temperature_c\":%.2f,"
        "\"temperature_valid\":%s,"
        "\"time_synced\":%s,"
        "\"time_source\":\"%s\","
        "\"time_last_sync\":%s,"
        "\"now\":%lu,"
        "\"tz_name\":\"%s\","
        "\"time_mode\":\"%s\","
        "\"temp_unit\":\"%s\","
        "\"wifi\":{"
            "\"state\":\"%s\","
            "\"ssid\":\"%s\","
            "\"rssi\":%d,"
            "\"ip\":\"%s\""
        "},"
        "\"fw_version\":\"%s\","
        "\"uptime_s\":%lu,"
        "\"history_records\":%lu,"
        "\"storage_free_kb\":%lu"
        "}",
        (double)reading.value_c,
        reading.valid ? "true" : "false",
        time_synced   ? "true" : "false",
        tsrc_str[tsrc_idx],
        last_sync_str,
        (unsigned long)now,
        tz_name,
        time_mode_str,
        temp_unit_str,
        wifi_state_str[ws_idx],
        sta_ssid,
        rssi,
        ip_str,
        desc ? desc->version : "unknown",
        (unsigned long)uptime_s,
        (unsigned long)history_record_count(),
        (unsigned long)free_kb);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ── PUT /api/config ── */
static esp_err_t api_config_put(httpd_req_t *req)
{
    char body[256] = "";
    int n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) { httpd_resp_send_500(req); return ESP_FAIL; }
    body[n] = '\0';

    char tz_name[64] = "", time_mode[8] = "", temp_unit[4] = "";
    json_str(body, "tz_name",   tz_name,   sizeof(tz_name));
    json_str(body, "time_mode", time_mode, sizeof(time_mode));
    json_str(body, "temp_unit", temp_unit, sizeof(temp_unit));

    if (tz_name[0]) {
        if (settings_set_timezone(tz_name) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"unknown_timezone\"}");
            return ESP_FAIL;
        }
    }
    if (time_mode[0]) {
        if (strcmp(time_mode, "utc") == 0)
            settings_set_time_mode(TIME_MODE_UTC);
        else if (strcmp(time_mode, "local") == 0)
            settings_set_time_mode(TIME_MODE_LOCAL);
        else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"invalid_time_mode\"}");
            return ESP_FAIL;
        }
    }
    if (temp_unit[0]) {
        if (strcmp(temp_unit, "F") == 0)
            settings_set_temp_unit(TEMP_UNIT_FAHRENHEIT);
        else if (strcmp(temp_unit, "C") == 0)
            settings_set_temp_unit(TEMP_UNIT_CELSIUS);
        else {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"invalid_temp_unit\"}");
            return ESP_FAIL;
        }
    }

    /* Return effective config */
    return api_status(req);
}

/* ── History iterator callback (JSON streaming) ── */
typedef struct { httpd_req_t *req; bool first; } hist_json_ctx_t;

static void hist_json_cb(uint32_t epoch, float temp_c, void *ctx_ptr)
{
    hist_json_ctx_t *ctx = (hist_json_ctx_t *)ctx_ptr;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s{\"timestamp\":%lu,\"temperature\":%.2f}",
             ctx->first ? "" : ",", (unsigned long)epoch, (double)temp_c);
    ctx->first = false;
    httpd_resp_sendstr_chunk(ctx->req, buf);
}

static esp_err_t api_history_get(httpd_req_t *req)
{
    /* Parse optional query params: from=<epoch>&to=<epoch> */
    char query[64] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    uint32_t from = 0, to = UINT32_MAX;
    char val[16];
    if (httpd_query_key_value(query, "from", val, sizeof(val)) == ESP_OK)
        from = (uint32_t)strtoul(val, NULL, 10);
    if (httpd_query_key_value(query, "to", val, sizeof(val)) == ESP_OK)
        to   = (uint32_t)strtoul(val, NULL, 10);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"records\":[");
    hist_json_ctx_t ctx = {.req = req, .first = true};
    history_query(from, to, hist_json_cb, &ctx);
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* ── History CSV download ── */
typedef struct { httpd_req_t *req; } hist_csv_ctx_t;

static void hist_csv_cb(uint32_t epoch, float temp_c, void *ctx_ptr)
{
    hist_csv_ctx_t *ctx = (hist_csv_ctx_t *)ctx_ptr;
    struct tm t;
    time_t ts = (time_t)epoch;
    gmtime_r(&ts, &t);
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ,%.2f\n",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, (double)temp_c);
    httpd_resp_sendstr_chunk(ctx->req, buf);
}

static esp_err_t api_history_csv(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"history.csv\"");
    httpd_resp_sendstr_chunk(req, "timestamp_iso8601,temperature_c\n");
    hist_csv_ctx_t ctx = {.req = req};
    history_query(0, UINT32_MAX, hist_csv_cb, &ctx);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

void register_mgmt_handlers(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        {.uri="/",                .method=HTTP_GET,  .handler=mgmt_page},
        {.uri="/mgmt.css",        .method=HTTP_GET,  .handler=mgmt_css},
        {.uri="/mgmt.js",         .method=HTTP_GET,  .handler=mgmt_js},
        {.uri="/api/status",      .method=HTTP_GET,  .handler=api_status},
        {.uri="/api/config",      .method=HTTP_PUT,  .handler=api_config_put},
        {.uri="/api/history",     .method=HTTP_GET,  .handler=api_history_get},
        {.uri="/api/history.csv", .method=HTTP_GET,  .handler=api_history_csv},
    };
    for (int i = 0; i < 7; i++) httpd_register_uri_handler(server, &uris[i]);
    register_timezones_handler(server);
    register_ota_handlers(server);
}
