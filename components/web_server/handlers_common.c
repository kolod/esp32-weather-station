#include "handlers_common.h"
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
