#include "handlers_mgmt.h"
#include "handlers_common.h"
#include "handlers_ota.h"
#include "ws_broadcast.h"
#include "app_ctx.h"
#include "settings.h"
#include "history.h"
#include "rtc_time.h"
#include "boot_log.h"
#include "tz_table.h"
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_vfs.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "sys/statvfs.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "handlers_mgmt"

/* Embedded assets injected via CMakeLists EMBED_FILES (stored uncompressed) */
extern const uint8_t mgmt_html_start[] asm("_binary_mgmt_html_start");
extern const uint8_t mgmt_html_end[]   asm("_binary_mgmt_html_end");
extern const uint8_t mgmt_css_start[]  asm("_binary_mgmt_css_start");
extern const uint8_t mgmt_css_end[]    asm("_binary_mgmt_css_end");
extern const uint8_t mgmt_js_start[]   asm("_binary_mgmt_js_start");
extern const uint8_t mgmt_js_end[]     asm("_binary_mgmt_js_end");
extern const uint8_t chart_js_start[]  asm("_binary_chart_js_start");
extern const uint8_t chart_js_end[]    asm("_binary_chart_js_end");

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
    httpd_resp_set_hdr(req, "Strict-Transport-Security", "max-age=31536000");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "upgrade-insecure-requests; default-src 'self'");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");

    char lang[3];
    pick_request_lang(req, lang);
    return send_html_lang_patched(req, mgmt_html_start, mgmt_html_end, lang);
}
static esp_err_t mgmt_css(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)mgmt_css_start,
                    mgmt_css_end - mgmt_css_start);
    return ESP_OK;
}
static esp_err_t mgmt_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)mgmt_js_start,
                    mgmt_js_end - mgmt_js_start);
    return ESP_OK;
}
static esp_err_t chart_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, (const char *)chart_js_start,
                    chart_js_end - chart_js_start);
    return ESP_OK;
}

/* ── Status snapshot builder (shared by GET /api/status and the /api/ws
   broadcaster, feature 009) ── */
esp_err_t build_status_json(char *buf, size_t buf_len)
{
    if (buf_len < 1152) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    temperature_reading_t reading = app_state.reading;
    pressure_reading_t pressure   = app_state.pressure;
    humidity_reading_t humidity   = app_state.humidity;
    sensor_kind_t sensor_kind     = app_state.sensor_kind;
    wifi_state_t ws               = app_state.wifi_state;
    bool time_synced              = app_state.time_synced;
    app_time_source_t tsrc        = app_state.time_source;
    xSemaphoreGive(app_state_mutex);

    static const char *sensor_str[] = {"none", "ds18b20", "bmp280", "bme280"};
    int sensor_idx = ((int)sensor_kind >= 0 && (int)sensor_kind < 4) ? (int)sensor_kind : 0;

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
    int ws_idx = ((int)ws >= 0 && (int)ws < 6) ? (int)ws : 0;
    snprintf(buf, buf_len,
        "{"
        "\"temperature_c\":%.2f,"
        "\"temperature_valid\":%s,"
        "\"pressure_hpa\":%.1f,"
        "\"pressure_valid\":%s,"
        "\"humidity_pct\":%.1f,"
        "\"humidity_valid\":%s,"
        "\"sensor\":\"%s\","
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
        (double)pressure.value_hpa,
        pressure.valid ? "true" : "false",
        (double)humidity.value_pct,
        humidity.valid ? "true" : "false",
        sensor_str[sensor_idx],
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

    return ESP_OK;
}

/* ── GET /api/status ── */
static esp_err_t api_status(httpd_req_t *req)
{
    char buf[1152];
    esp_err_t err = build_status_json(buf, sizeof(buf));
    if (err != ESP_OK) { httpd_resp_send_500(req); return ESP_FAIL; }
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

static void hist_json_cb(uint32_t epoch, float temp_c, float pressure_hpa,
                         float humidity_pct, void *ctx_ptr)
{
    hist_json_ctx_t *ctx = (hist_json_ctx_t *)ctx_ptr;
    char press[16], hum[16];
    if (isnan(pressure_hpa))
        strlcpy(press, "null", sizeof(press));
    else
        snprintf(press, sizeof(press), "%.1f", (double)pressure_hpa);
    if (isnan(humidity_pct))
        strlcpy(hum, "null", sizeof(hum));
    else
        snprintf(hum, sizeof(hum), "%.1f", (double)humidity_pct);

    char buf[128];
    snprintf(buf, sizeof(buf),
             "%s{\"timestamp\":%lu,\"temperature\":%.2f,\"pressure\":%s,\"humidity\":%s}",
             ctx->first ? "" : ",", (unsigned long)epoch, (double)temp_c, press, hum);
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

static void hist_csv_cb(uint32_t epoch, float temp_c, float pressure_hpa,
                        float humidity_pct, void *ctx_ptr)
{
    hist_csv_ctx_t *ctx = (hist_csv_ctx_t *)ctx_ptr;
    struct tm t;
    time_t ts = (time_t)epoch;
    gmtime_r(&ts, &t);
    char press[16] = ""; /* empty cell when the sample has no pressure */
    char hum[16]   = ""; /* empty cell when the sample has no humidity */
    if (!isnan(pressure_hpa))
        snprintf(press, sizeof(press), "%.1f", (double)pressure_hpa);
    if (!isnan(humidity_pct))
        snprintf(hum, sizeof(hum), "%.1f", (double)humidity_pct);
    char buf[96];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ,%.2f,%s,%s\n",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, (double)temp_c, press, hum);
    httpd_resp_sendstr_chunk(ctx->req, buf);
}

static esp_err_t api_history_csv(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"history.csv\"");
    httpd_resp_sendstr_chunk(req,
        "timestamp_iso8601,temperature_c,pressure_hpa,humidity_pct\n");
    hist_csv_ctx_t ctx = {.req = req};
    history_query(0, UINT32_MAX, hist_csv_cb, &ctx);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

/* ── GET /api/boot.log (feature 006) ──
   Source of truth is /storage/boot.log; before the boot-log flush (or if the
   flush failed) the live RAM capture is served instead, so view and download
   always return the same bytes. 404 only when file AND buffer are empty. */
static esp_err_t api_bootlog(httpd_req_t *req)
{
    char chunk[256];
    FILE *f = fopen(BOOT_LOG_FILE_PATH, "r");
    if (f) {
        size_t n = fread(chunk, 1, sizeof(chunk), f);
        if (n > 0) {
            httpd_resp_set_type(req, "text/plain; charset=utf-8");
            do {
                httpd_resp_send_chunk(req, chunk, n);
                n = fread(chunk, 1, sizeof(chunk), f);
            } while (n > 0);
            fclose(f);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_OK;
        }
        fclose(f);
    }

    size_t len = 0;
    const char *ram = boot_log_get(&len);
    if (len > 0) {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_send(req, ram, len);
        return ESP_OK;
    }

    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"not_found\"}");
    return ESP_OK;
}

/* ── GET /api/ws — live-readings WebSocket (feature 009) ──
   esp_http_server completes the handshake before this runs; on the handshake
   request (HTTP_GET) there is nothing to do — ws_broadcast discovers clients
   from the server directly. The client never needs to send, so any frame that
   does arrive is just drained. Dead sockets are pruned by ws_broadcast via
   httpd_ws_get_fd_info() / send failure. */
static esp_err_t api_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        return ESP_OK;
    }

    httpd_ws_frame_t frame = {.type = HTTPD_WS_TYPE_TEXT};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0); /* length probe */
    if (err != ESP_OK) return err;
    if (frame.len) {
        uint8_t *b = calloc(1, frame.len + 1);
        if (b) {
            frame.payload = b;
            httpd_ws_recv_frame(req, &frame, frame.len);
            free(b);
        }
    }
    return ESP_OK;
}

void register_mgmt_handlers(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        {.uri="/",                .method=HTTP_GET,  .handler=mgmt_page},
        {.uri="/mgmt.css",        .method=HTTP_GET,  .handler=mgmt_css},
        {.uri="/mgmt.js",         .method=HTTP_GET,  .handler=mgmt_js},
        {.uri="/chart.js",        .method=HTTP_GET,  .handler=chart_js},
        {.uri="/api/status",      .method=HTTP_GET,  .handler=api_status},
        {.uri="/api/config",      .method=HTTP_PUT,  .handler=api_config_put},
        {.uri="/api/history",     .method=HTTP_GET,  .handler=api_history_get},
        {.uri="/api/history.csv", .method=HTTP_GET,  .handler=api_history_csv},
        {.uri="/api/boot.log",    .method=HTTP_GET,  .handler=api_bootlog},
    };
    for (int i = 0; i < 9; i++) httpd_register_uri_handler(server, &uris[i]);

    httpd_uri_t ws = {
        .uri          = "/api/ws",
        .method       = HTTP_GET,
        .handler      = api_ws,
        .is_websocket = true,
    };
    httpd_register_uri_handler(server, &ws);

    register_timezones_handler(server);
    register_i18n_handlers(server); /* language packs + /i18n.js */
    register_ota_handlers(server);
}
