#include "portal_server.h"
#include "handlers_common.h"
#include "i18n.h"
#include "wifi_mgr.h"
#include "settings.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

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

#define TAG "portal"

/* Embedded assets injected via CMakeLists EMBED_FILES */
extern const uint8_t portal_html_gz_start[] asm("_binary_index_html_start");
extern const uint8_t portal_html_gz_end[]   asm("_binary_index_html_end");
extern const uint8_t portal_css_gz_start[]  asm("_binary_portal_css_start");
extern const uint8_t portal_css_gz_end[]    asm("_binary_portal_css_end");
extern const uint8_t portal_js_gz_start[]   asm("_binary_portal_js_start");
extern const uint8_t portal_js_gz_end[]     asm("_binary_portal_js_end");

static httpd_handle_t s_server = NULL;

/* ── Captive portal probe redirect ── */
static esp_err_t captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

/* ── Portal page (GET /) ── */
static esp_err_t portal_page(httpd_req_t *req)
{
    char accept_lang[64] = "";
    httpd_req_get_hdr_value_str(req, "Accept-Language", accept_lang, sizeof(accept_lang));

    char lang[3];
    accept_language_pick(accept_lang, lang, sizeof(lang));

    /* Serve gzip-compressed HTML; inject lang attribute via minimal edit is complex,
       so the JS reads the lang from the data-lang attribute we set via a tiny inline script. */
    char lang_hdr[32];
    snprintf(lang_hdr, sizeof(lang_hdr), "%s", lang);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Language", lang_hdr);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    size_t len = portal_html_gz_end - portal_html_gz_start;
    httpd_resp_send(req, (const char *)portal_html_gz_start, len);
    return ESP_OK;
}

/* ── Serve CSS/JS assets ── */
static esp_err_t portal_css(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, (const char *)portal_css_gz_start,
                    portal_css_gz_end - portal_css_gz_start);
    return ESP_OK;
}
static esp_err_t portal_js(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, (const char *)portal_js_gz_start,
                    portal_js_gz_end - portal_js_gz_start);
    return ESP_OK;
}

/* ── GET /api/scan ── */
static esp_err_t api_scan(httpd_req_t *req)
{
    wifi_scan_config_t scan_cfg = {.show_hidden = false};
    esp_wifi_scan_start(&scan_cfg, true); /* blocking */

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;

    wifi_ap_record_t *aps = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (!aps) { httpd_resp_send_500(req); return ESP_FAIL; }
    esp_wifi_scan_get_ap_records(&ap_count, aps);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"networks\":[");
    for (int i = 0; i < ap_count; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s{\"ssid\":\"%s\",\"rssi\":%d,\"secure\":%s}",
                 i > 0 ? "," : "",
                 (char *)aps[i].ssid, aps[i].rssi,
                 aps[i].authmode != WIFI_AUTH_OPEN ? "true" : "false");
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    free(aps);
    return ESP_OK;
}

/* ── POST /api/wifi ── */

typedef enum { JOIN_IDLE, JOIN_CONNECTING, JOIN_CONNECTED, JOIN_FAILED } join_state_t;
static volatile join_state_t s_join_state = JOIN_IDLE;
static volatile int          s_join_reason = 0; /* wifi_err_reason_t */
static char s_device_suffix[5] = "";

static void on_join_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    mac_to_suffix(s_device_suffix);
    s_join_state = JOIN_CONNECTED;
}
static void on_join_disconnected(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)data;
    s_join_reason = ev->reason;
    s_join_state  = JOIN_FAILED;
}

static esp_err_t api_wifi_post(httpd_req_t *req)
{
    char body[256] = "";
    int  n = httpd_req_recv(req, body, sizeof(body) - 1);
    if (n <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"empty_body\"}");
        return ESP_FAIL;
    }
    body[n] = '\0';

    char ssid[33] = "", password[64] = "", tz_name[64] = "";
    if (!json_str(body, "ssid", ssid, sizeof(ssid)) ||
        strlen(ssid) == 0 || strlen(ssid) > 32) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"invalid_ssid\"}");
        return ESP_FAIL;
    }
    json_str(body, "password", password, sizeof(password));
    if (strlen(password) > 63) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "{\"error\":\"password_too_long\"}");
        return ESP_FAIL;
    }

    /* Apply timezone if provided */
    if (json_str(body, "tz_name", tz_name, sizeof(tz_name)) && tz_name[0])
        settings_set_timezone(tz_name);

    /* Save credentials and trigger connect */
    wifi_config_t sta_cfg = {};
    strlcpy((char *)sta_cfg.sta.ssid,     ssid,     sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));

    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);

    /* Temporary one-shot event registrations to track this join attempt */
    s_join_state = JOIN_CONNECTING;
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP,          on_join_got_ip,       NULL);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,  on_join_disconnected, NULL);

    wifi_mgr_connect_sta();

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"connecting\"}");
    return ESP_OK;
}

/* ── GET /api/wifi/status ── */
static esp_err_t api_wifi_status(httpd_req_t *req)
{
    char buf[128];
    const char *state_str =
        s_join_state == JOIN_CONNECTED  ? "connected"  :
        s_join_state == JOIN_CONNECTING ? "connecting" : "failed";

    const char *reason_str =
        s_join_reason == 15 /* WIFI_REASON_AUTH_FAIL */ ? "auth" :
        s_join_reason == 201 /* WIFI_REASON_NO_AP_FOUND */ ? "not_found" : NULL;

    if (s_join_state == JOIN_CONNECTED) {
        snprintf(buf, sizeof(buf),
                 "{\"state\":\"connected\",\"suffix\":\"%s\",\"reason\":null}",
                 s_device_suffix);
    } else if (reason_str) {
        snprintf(buf, sizeof(buf),
                 "{\"state\":\"%s\",\"ip\":null,\"reason\":\"%s\"}", state_str, reason_str);
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"state\":\"%s\",\"ip\":null,\"reason\":null}", state_str);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

esp_err_t portal_server_start(void)
{
    if (s_server) return ESP_OK;

    httpd_config_t cfg     = HTTPD_DEFAULT_CONFIG();
    cfg.server_port        = 80;
    cfg.max_uri_handlers   = 16;
    cfg.uri_match_fn       = httpd_uri_match_wildcard;

    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start portal HTTP server");
        return ESP_FAIL;
    }

    /* Captive portal probes */
    const char *probe_uris[] = {"/generate_204", "/gen_204",
                                  "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt"};
    httpd_uri_t probe_uri = {.method = HTTP_GET, .handler = captive_redirect};
    for (int i = 0; i < 5; i++) {
        probe_uri.uri = probe_uris[i];
        httpd_register_uri_handler(s_server, &probe_uri);
    }

    httpd_uri_t uris[] = {
        {.uri="/",              .method=HTTP_GET,  .handler=portal_page},
        {.uri="/portal.css",    .method=HTTP_GET,  .handler=portal_css},
        {.uri="/portal.js",     .method=HTTP_GET,  .handler=portal_js},
        {.uri="/api/scan",      .method=HTTP_GET,  .handler=api_scan},
        {.uri="/api/wifi",      .method=HTTP_POST, .handler=api_wifi_post},
        {.uri="/api/wifi/status",.method=HTTP_GET, .handler=api_wifi_status},
    };
    for (int i = 0; i < 6; i++) httpd_register_uri_handler(s_server, &uris[i]);
    register_timezones_handler(s_server);

    ESP_LOGI(TAG, "Portal HTTP server started on :80");
    return ESP_OK;
}

void portal_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "Portal HTTP server stopped");
    }
}
