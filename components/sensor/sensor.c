#include "sensor.h"
#include "app_ctx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

/* espressif/ds18b20 managed component */
#include "onewire_bus.h"
#include "ds18b20.h"

#define TAG            "sensor"
#define SENSOR_GPIO    27
#define READ_PERIOD_MS 5000
#define TEMP_MIN_C     (-55.0f)
#define TEMP_MAX_C     (125.0f)

static onewire_bus_handle_t s_bus      = NULL;
static ds18b20_device_handle_t s_probe = NULL;

static bool init_probe(void)
{
    onewire_bus_config_t bus_cfg = {
        .bus_gpio_num = SENSOR_GPIO,
    };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,
    };
    if (onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_bus) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init 1-Wire RMT bus on GPIO%d", SENSOR_GPIO);
        return false;
    }

    onewire_device_iter_handle_t iter = NULL;
    if (onewire_new_device_iter(s_bus, &iter) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create device iterator");
        return false;
    }

    onewire_device_t dev;
    esp_err_t search_result = onewire_device_iter_get_next(iter, &dev);
    onewire_del_device_iter(iter);

    if (search_result != ESP_OK) {
        ESP_LOGW(TAG, "No DS18B20 device found on bus");
        return false;
    }

    ds18b20_config_t probe_cfg = {};
    if (ds18b20_new_device_from_enumeration(&dev, &probe_cfg, &s_probe) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create DS18B20 handle");
        return false;
    }

    ESP_LOGI(TAG, "DS18B20 found on GPIO%d, address: %016llX", SENSOR_GPIO,
             (unsigned long long)dev.address);
    return true;
}

static void sensor_task(void *arg)
{
    bool probe_ready = init_probe();

    while (true) {
        temperature_reading_t r = {
            .valid        = false,
            .value_c      = 0.0f,
            .updated_at_ms = esp_timer_get_time() / 1000,
        };

        if (!probe_ready) {
            /* Retry probe init on each cycle in case probe was reconnected */
            probe_ready = init_probe();
        }

        if (probe_ready) {
            float temp = 0.0f;
            esp_err_t err = ds18b20_trigger_temperature_conversion(s_probe);
            if (err == ESP_OK) {
                vTaskDelay(pdMS_TO_TICKS(800)); /* max conversion time at 12-bit */
                err = ds18b20_get_temperature(s_probe, &temp);
            }
            if (err == ESP_OK && temp >= TEMP_MIN_C && temp <= TEMP_MAX_C) {
                r.valid   = true;
                r.value_c = temp;
            } else {
                ESP_LOGW(TAG, "Bad reading: err=%s temp=%.2f", esp_err_to_name(err), temp);
                /* Probe may have been disconnected; reset handle on next cycle */
                if (err != ESP_OK) {
                    ds18b20_del_device(s_probe);
                    s_probe     = NULL;
                    probe_ready = false;
                }
            }
        }

        r.updated_at_ms = esp_timer_get_time() / 1000;

        xSemaphoreTake(app_state_mutex, portMAX_DELAY);
        app_state.reading = r;
        xSemaphoreGive(app_state_mutex);

        app_event_post(APP_EVT_READING_UPDATED);

        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS - 800));
    }
}

void sensor_start(void)
{
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
}
