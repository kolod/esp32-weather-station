#include "mgmt_server.h"
#include "handlers_mgmt.h"
#include "ws_broadcast.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define TAG       "mgmt_server"
#define CERT_PATH "/storage/certs/device.crt"
#define KEY_PATH  "/storage/certs/device.key"

static httpd_handle_t s_server = NULL;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 8192) { fclose(f); return NULL; }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

esp_err_t mgmt_server_start(void)
{
    if (s_server) return ESP_OK;

    char *cert = read_file(CERT_PATH);
    char *key  = read_file(KEY_PATH);
    if (!cert || !key) {
        ESP_LOGW(TAG, "Device certificate not provisioned at %s / %s — HTTPS skipped",
                 CERT_PATH, KEY_PATH);
        free(cert); free(key);
        return ESP_ERR_NOT_FOUND;
    }

    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    cfg.servercert     = (const uint8_t *)cert;
    cfg.servercert_len = strlen(cert) + 1;
    cfg.prvtkey_pem    = (const uint8_t *)key;
    cfg.prvtkey_len    = strlen(key) + 1;
    cfg.httpd.max_uri_handlers = 20;
    /* feature 009: a live-readings WebSocket holds a socket open for the life of
       the page; 5 covers ~4 viewers plus one transient request. See
       specs/009-mgmt-page-redesign/plan.md Complexity Tracking. */
    cfg.httpd.max_open_sockets = 5;
    cfg.httpd.uri_match_fn     = httpd_uri_match_wildcard; /* i18n pack route */

    esp_err_t err = httpd_ssl_start(&s_server, &cfg);
    free(cert);
    free(key);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS server: %s", esp_err_to_name(err));
        return err;
    }

    register_mgmt_handlers(s_server);
    ws_broadcast_start(s_server); /* feature 009: live-readings push */
    ESP_LOGI(TAG, "HTTPS management server started on :443");
    return ESP_OK;
}

void mgmt_server_stop(void)
{
    if (s_server) {
        ws_broadcast_stop();
        httpd_ssl_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTPS management server stopped");
    }
}
