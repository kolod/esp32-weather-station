#include "handlers_common.h"
#include "i18n.h"
#include "tz_table.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG "handlers_common"
#define CHUNK 256

static esp_err_t timezones_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    httpd_resp_sendstr_chunk(req, "[");
    int count = tz_table_count();
    for (int i = 0; i < count; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s{\"name\":\"%s\"}", i > 0 ? "," : "",
                 tz_table_name(i));
        httpd_resp_sendstr_chunk(req, buf);
    }
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL); /* end chunked */
    return ESP_OK;
}

void register_timezones_handler(httpd_handle_t server)
{
    httpd_uri_t uri = {
        .uri     = "/api/timezones",
        .method  = HTTP_GET,
        .handler = timezones_get,
    };
    httpd_register_uri_handler(server, &uri);
}

/* ── Language selection (Accept-Language → 2-char code) ── */

void pick_request_lang(httpd_req_t *req, char lang[3])
{
    /* Real browsers send well over 64 bytes of Accept-Language; consider up
       to 255 chars and accept a truncated read rather than failing. */
    char header[256] = "";
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Accept-Language",
                                                header, sizeof(header));
    if (err != ESP_OK && err != ESP_ERR_HTTPD_RESULT_TRUNC) {
        header[0] = '\0';
    }
    accept_language_pick(header, lang, 3);
}

/* ── Localized page send (patch <html lang="…">) ── */

esp_err_t send_html_lang_patched(httpd_req_t *req, const uint8_t *start,
                                 const uint8_t *end, const char lang[3])
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Language", lang);
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    /* Locate the 2-char value of the first lang="…" attribute. */
    static const char marker[] = "lang=\"";
    const size_t mlen  = sizeof(marker) - 1;
    const size_t total = (size_t)(end - start);
    const uint8_t *val = NULL;
    for (size_t i = 0; i + mlen + 2 <= total; i++) {
        if (memcmp(start + i, marker, mlen) == 0) {
            val = start + i + mlen;
            break;
        }
    }
    if (!val) {
        ESP_LOGW(TAG, "embedded page has no lang attribute");
        return httpd_resp_send(req, (const char *)start, total);
    }

    esp_err_t err = httpd_resp_send_chunk(req, (const char *)start,
                                          val - start);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, lang, 2);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, (const char *)val + 2,
                                                   (end - val) - 2);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

/* ── Language packs (/i18n/<code>.json) and shared applier (/i18n.js) ── */

extern const uint8_t i18n_en_start[] asm("_binary_en_json_start");
extern const uint8_t i18n_en_end[]   asm("_binary_en_json_end");
extern const uint8_t i18n_de_start[] asm("_binary_de_json_start");
extern const uint8_t i18n_de_end[]   asm("_binary_de_json_end");
extern const uint8_t i18n_fr_start[] asm("_binary_fr_json_start");
extern const uint8_t i18n_fr_end[]   asm("_binary_fr_json_end");
extern const uint8_t i18n_uk_start[] asm("_binary_uk_json_start");
extern const uint8_t i18n_uk_end[]   asm("_binary_uk_json_end");
extern const uint8_t i18n_js_start[] asm("_binary_i18n_js_start");
extern const uint8_t i18n_js_end[]   asm("_binary_i18n_js_end");

static esp_err_t i18n_pack_get(httpd_req_t *req)
{
    static const struct {
        const char    *uri;
        const uint8_t *start;
        const uint8_t *end;
    } packs[] = {
        {"/i18n/en.json", i18n_en_start, i18n_en_end},
        {"/i18n/de.json", i18n_de_start, i18n_de_end},
        {"/i18n/fr.json", i18n_fr_start, i18n_fr_end},
        {"/i18n/uk.json", i18n_uk_start, i18n_uk_end},
    };

    /* Compare the path only (ignore any query string). */
    char path[32];
    strlcpy(path, req->uri, sizeof(path));
    char *q = strchr(path, '?');
    if (q) *q = '\0';

    for (size_t i = 0; i < sizeof(packs) / sizeof(packs[0]); i++) {
        if (strcmp(path, packs[i].uri) == 0) {
            httpd_resp_set_type(req, "application/json; charset=utf-8");
            httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
            return httpd_resp_send(req, (const char *)packs[i].start,
                                   packs[i].end - packs[i].start);
        }
    }
    /* Unsupported language or bogus path: 404 without echoing the URI. */
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "{\"error\":\"not_found\"}");
    return ESP_FAIL;
}

static esp_err_t i18n_js_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    return httpd_resp_send(req, (const char *)i18n_js_start,
                           i18n_js_end - i18n_js_start);
}

void register_i18n_handlers(httpd_handle_t server)
{
    httpd_uri_t packs = {
        .uri     = "/i18n/*",
        .method  = HTTP_GET,
        .handler = i18n_pack_get,
    };
    httpd_uri_t script = {
        .uri     = "/i18n.js",
        .method  = HTTP_GET,
        .handler = i18n_js_get,
    };
    httpd_register_uri_handler(server, &packs);
    httpd_register_uri_handler(server, &script);
}
