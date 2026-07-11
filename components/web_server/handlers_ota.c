#include "handlers_ota.h"
#include "app_ctx.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG         "ota"
#define OTA_MAX_SIZE (3 * 1024 * 1024) /* 3 MB */
#define CHUNK_SIZE   4096

static ota_session_t s_session = {.state = OTA_ST_IDLE};
static SemaphoreHandle_t s_mutex = NULL;

static void init_mutex(void)
{
    if (!s_mutex) s_mutex = xSemaphoreCreateMutex();
}

static void set_session(ota_state_t st, uint8_t pct, const char *err)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_session.state        = st;
    s_session.progress_pct = pct;
    if (err) strlcpy(s_session.error, err, sizeof(s_session.error));
    else      s_session.error[0] = '\0';
    xSemaphoreGive(s_mutex);

    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    app_state.ota = s_session;
    xSemaphoreGive(app_state_mutex);
    app_event_post(APP_EVT_OTA_STATE_CHANGED);
}

static void reboot_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
    vTaskDelete(NULL);
}

/* ── POST /api/ota ── */
static esp_err_t api_ota_post(httpd_req_t *req)
{
    init_mutex();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    ota_state_t cur = s_session.state;
    xSemaphoreGive(s_mutex);

    if (cur == OTA_ST_RECEIVING) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "{\"error\":\"update_in_progress\"}");
        return ESP_FAIL;
    }

    int content_len = req->content_len;
    if (content_len <= 0 || content_len > OTA_MAX_SIZE) {
        httpd_resp_set_status(req, "507 Insufficient Storage");
        httpd_resp_sendstr(req, "{\"error\":\"image_too_large\"}");
        return ESP_FAIL;
    }

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_part, content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    set_session(OTA_ST_RECEIVING, 0, NULL);

    char *buf = malloc(CHUNK_SIZE);
    if (!buf) { esp_ota_end(ota_handle); httpd_resp_send_500(req); return ESP_FAIL; }

    int received = 0;
    while (received < content_len) {
        int to_read = content_len - received;
        if (to_read > CHUNK_SIZE) to_read = CHUNK_SIZE;
        int n = httpd_req_recv(req, buf, to_read);
        if (n <= 0) {
            ESP_LOGE(TAG, "recv error");
            goto fail;
        }
        if (esp_ota_write(ota_handle, buf, n) != ESP_OK) {
            ESP_LOGE(TAG, "ota_write error");
            goto fail;
        }
        received += n;
        set_session(OTA_ST_RECEIVING, (uint8_t)(received * 100 / content_len), NULL);
    }
    free(buf); buf = NULL;

    /* Validate */
    set_session(OTA_ST_VALIDATING, 100, NULL);
    if (esp_ota_end(ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end validation failed");
        set_session(OTA_ST_FAILED, 0, "invalid_image");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"invalid_image\",\"message\":\"Image validation failed\"}");
        return ESP_FAIL;
    }

    /* Verify app descriptor project name matches */
    esp_app_desc_t new_desc;
    if (esp_ota_get_partition_description(update_part, &new_desc) == ESP_OK) {
        const esp_app_desc_t *cur_desc = esp_app_get_description();
        if (cur_desc && strcmp(new_desc.project_name, cur_desc->project_name) != 0) {
            ESP_LOGE(TAG, "Project name mismatch: got '%s'", new_desc.project_name);
            set_session(OTA_ST_FAILED, 0, "invalid_image");
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"error\":\"invalid_image\",\"message\":\"Wrong firmware target\"}");
            return ESP_FAIL;
        }
    }

    if (esp_ota_set_boot_partition(update_part) != ESP_OK) {
        set_session(OTA_ST_FAILED, 0, "set_boot_failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    set_session(OTA_ST_APPLIED_PENDING_REBOOT, 100, NULL);
    ESP_LOGI(TAG, "OTA update applied, rebooting in 3 s");
    xTaskCreate(reboot_task, "ota_reboot", 2048, NULL, 3, NULL);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"applied\",\"reboot_in_s\":3}");
    return ESP_OK;

fail:
    free(buf);
    esp_ota_end(ota_handle);
    set_session(OTA_ST_FAILED, 0, "write_error");
    httpd_resp_send_500(req);
    return ESP_FAIL;
}

/* ── GET /api/ota/status ── */
static esp_err_t api_ota_status(httpd_req_t *req)
{
    init_mutex();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    ota_session_t snap = s_session;
    xSemaphoreGive(s_mutex);

    const char *state_str[] = {
        "idle", "receiving", "validating", "applied_pending_reboot", "failed"
    };
    char buf[200];
    snprintf(buf, sizeof(buf),
             "{\"state\":\"%s\",\"progress_pct\":%d,\"error\":%s%s%s}",
             state_str[(int)snap.state],
             snap.progress_pct,
             snap.error[0] ? "\"" : "null",
             snap.error[0] ? snap.error : "",
             snap.error[0] ? "\"" : "");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

void register_ota_handlers(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        {.uri="/api/ota",        .method=HTTP_POST, .handler=api_ota_post},
        {.uri="/api/ota/status", .method=HTTP_GET,  .handler=api_ota_status},
    };
    for (int i = 0; i < 2; i++) httpd_register_uri_handler(server, &uris[i]);
}
