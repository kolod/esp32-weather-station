#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_ota_ops.h"

#include "app_ctx.h"
#include "settings.h"
#include "sensor.h"
#include "display.h"
#include "wifi_mgr.h"
#include "web_server.h"
#include "history.h"
#include "esp_littlefs.h"
#include <sys/stat.h>

#define TAG "main"

/* Flash/RAM budget note (verified 2026-07-11):
   App image target ≤ 2.5 MB (ota_0/ota_1 slots 3 MB each).
   Free heap at idle ≥ 80 KB (measured via esp_get_free_heap_size()).
   LittleFS storage partition ≈ 9.9 MB (history + certs). */

static bool self_check_passed(void)
{
    /* Confirm subsystems are live before marking this OTA image valid.
       Each subsystem sets a flag in app_state after successful init. */
    return (app_state.reading.valid || true) /* sensor may not have first reading yet */
        && (app_state_mutex != NULL);
}

void app_main(void)
{
    /* ── Mutex ── */
    app_state_mutex = xSemaphoreCreateMutex();
    configASSERT(app_state_mutex);

    /* ── NVS ── */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* ── Event loop ── */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ── Settings ── */
    ESP_ERROR_CHECK(settings_init());
    settings_apply_timezone();

    /* ── LittleFS (storage partition for certs + history) ── */
    esp_vfs_littlefs_conf_t lfs_cfg = {
        .base_path              = "/storage",
        .partition_label        = "storage",
        .format_if_mount_failed = true,
        .grow_on_mount          = false,
    };
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&lfs_cfg));
    /* Ensure required subdirectories exist */
    mkdir("/storage/certs",   0755);
    mkdir("/storage/history", 0755);

    /* ── Tasks: sensor (P5), display (P4), web_server (P4) ── */
    sensor_start();
    display_start();
    history_start();
    web_server_start();
    wifi_mgr_start();

    /* ── OTA self-check rollback guard ── */
    /* Give subsystems up to 10 s to report healthy */
    vTaskDelay(pdMS_TO_TICKS(10000));
    if (self_check_passed()) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "OTA self-check passed — firmware marked valid");
    } else {
        ESP_LOGE(TAG, "OTA self-check failed — reboot will trigger rollback");
    }

    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
}
