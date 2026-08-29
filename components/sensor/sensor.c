#include "sensor.h"
#include "bmp280.h"
#include "app_ctx.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"

/* espressif/ds18b20 managed component */
#include "onewire_bus.h"
#include "ds18b20.h"

#define TAG            "sensor"
#define PROBE_GPIO     27
#define I2C_SCL_GPIO   22
#define I2C_SDA_GPIO   21
#define READ_PERIOD_MS 5000
#define TEMP_MIN_C     (-55.0f)
#define TEMP_MAX_C     (125.0f)
#define BMP_TEMP_MIN_C (-40.0f)   /* BMP280/BME280 operating range */
#define BMP_TEMP_MAX_C (85.0f)
#define PRESS_MIN_HPA  (300.0f)
#define PRESS_MAX_HPA  (1100.0f)
#define HUM_MIN_PCT    (0.0f)     /* inclusive: 0 %RH is valid (bone dry)     */
#define HUM_MAX_PCT    (100.0f)   /* inclusive: 100 %RH is valid (saturated)  */

static i2c_master_bus_handle_t s_i2c_bus  = NULL;
static bmp280_t                s_bmp;
static bool                    s_bmp_ready = false;

static onewire_bus_handle_t    s_bus   = NULL;
static ds18b20_device_handle_t s_probe = NULL;

static sensor_kind_t s_kind = SENSOR_NONE;

/* ── BMP280 path ── */

static bool init_bmp280(void)
{
    if (!s_i2c_bus) {
        i2c_master_bus_config_t bus_cfg = {
            .i2c_port          = I2C_NUM_0,
            .scl_io_num        = I2C_SCL_GPIO,
            .sda_io_num        = I2C_SDA_GPIO,
            .clk_source        = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        if (i2c_new_master_bus(&bus_cfg, &s_i2c_bus) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus (SCL=%d SDA=%d)",
                     I2C_SCL_GPIO, I2C_SDA_GPIO);
            return false;
        }
    }
    if (bmp280_detect(s_i2c_bus, &s_bmp) != ESP_OK)
        return false;
    if (bmp280_configure(&s_bmp) != ESP_OK) {
        ESP_LOGW(TAG, "BMP280 configuration failed");
        bmp280_release(&s_bmp);
        return false;
    }
    return true;
}

/* ── DS18B20 path (unchanged behavior) ── */

static bool init_probe(void)
{
    if (!s_bus) {
        onewire_bus_config_t bus_cfg = {
            .bus_gpio_num = PROBE_GPIO,
        };
        onewire_bus_rmt_config_t rmt_cfg = {
            .max_rx_bytes = 10,
        };
        if (onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &s_bus) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init 1-Wire RMT bus on GPIO%d", PROBE_GPIO);
            return false;
        }
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

    ESP_LOGI(TAG, "DS18B20 found on GPIO%d, address: %016llX", PROBE_GPIO,
             (unsigned long long)dev.address);
    return true;
}

/* ── Per-cycle reads ── */

static void read_bmp280(temperature_reading_t *t, pressure_reading_t *p,
                        humidity_reading_t *h)
{
    if (!s_bmp_ready) {
        /* Same-sensor recovery only; never falls back to the probe (D6). */
        s_bmp_ready = init_bmp280();
        if (!s_bmp_ready) return;
    }

    float temp = 0.0f, press = 0.0f, hum = 0.0f;
    float *hum_out = s_bmp.has_humidity ? &hum : NULL;
    esp_err_t err = bmp280_read(&s_bmp, &temp, &press, hum_out);
    if (err == ESP_OK &&
        temp >= BMP_TEMP_MIN_C && temp <= BMP_TEMP_MAX_C &&
        press >= PRESS_MIN_HPA && press <= PRESS_MAX_HPA) {
        t->valid = true;  t->value_c   = temp;
        p->valid = true;  p->value_hpa = press;
        /* Humidity is independently range-checked: a saturated (100 %RH) or
           bone-dry (0 %RH) reading is valid; only NaN / out-of-range is not. */
        if (hum_out && hum >= HUM_MIN_PCT && hum <= HUM_MAX_PCT) {
            h->valid = true;  h->value_pct = hum;
        }
    } else {
        ESP_LOGW(TAG, "Bad %s reading: err=%s temp=%.2f press=%.1f",
                 s_bmp.has_humidity ? "BME280" : "BMP280",
                 esp_err_to_name(err), temp, press);
        if (err != ESP_OK) {
            bmp280_release(&s_bmp);
            s_bmp_ready = false;
        }
    }
}

static void read_probe(temperature_reading_t *t, bool *probe_ready)
{
    if (!*probe_ready) {
        /* Retry probe init on each cycle in case probe was reconnected */
        *probe_ready = init_probe();
        if (!*probe_ready) return;
    }

    float temp = 0.0f;
    esp_err_t err = ds18b20_trigger_temperature_conversion(s_probe);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(800)); /* max conversion time at 12-bit */
        err = ds18b20_get_temperature(s_probe, &temp);
    }
    if (err == ESP_OK && temp >= TEMP_MIN_C && temp <= TEMP_MAX_C) {
        t->valid   = true;
        t->value_c = temp;
    } else {
        ESP_LOGW(TAG, "Bad reading: err=%s temp=%.2f", esp_err_to_name(err), temp);
        /* Probe may have been disconnected; reset handle on next cycle */
        if (err != ESP_OK) {
            ds18b20_del_device(s_probe);
            s_probe      = NULL;
            *probe_ready = false;
        }
    }
}

static void sensor_task(void *arg)
{
    bool probe_ready = false;

    /* ── Boot-time detection (feature 005/007, D2/D6): BME280 → BMP280 →
       DS18B20 → none. The result is fixed until reboot; only same-sensor
       recovery happens at runtime. ── */
    s_bmp_ready = init_bmp280();
    if (s_bmp_ready) {
        s_kind = s_bmp.has_humidity ? SENSOR_BME280 : SENSOR_BMP280;
        ESP_LOGI(TAG, "Sensor mode: %s",
                 s_bmp.has_humidity ? "BME280 (temperature + pressure + humidity)"
                                    : "BMP280 (temperature + pressure)");
    } else {
        probe_ready = init_probe();
        s_kind = probe_ready ? SENSOR_DS18B20 : SENSOR_NONE;
        ESP_LOGI(TAG, "Sensor mode: %s",
                 probe_ready ? "DS18B20 (temperature only)" : "none detected");
    }

    xSemaphoreTake(app_state_mutex, portMAX_DELAY);
    app_state.sensor_kind = s_kind;
    xSemaphoreGive(app_state_mutex);

    while (true) {
        temperature_reading_t t = {.valid = false};
        pressure_reading_t    p = {.valid = false};
        humidity_reading_t    h = {.valid = false};

        if (s_kind == SENSOR_BMP280 || s_kind == SENSOR_BME280) {
            read_bmp280(&t, &p, &h);
        } else if (s_kind == SENSOR_DS18B20) {
            read_probe(&t, &probe_ready);
        }

        int64_t now_ms  = esp_timer_get_time() / 1000;
        t.updated_at_ms = now_ms;
        p.updated_at_ms = now_ms;
        h.updated_at_ms = now_ms;

        xSemaphoreTake(app_state_mutex, portMAX_DELAY);
        app_state.reading  = t;
        app_state.pressure = p;
        app_state.humidity = h;
        xSemaphoreGive(app_state_mutex);

        app_event_post(APP_EVT_READING_UPDATED);

        /* DS18B20 conversion already consumed 800 ms of this cycle */
        int delay_ms = READ_PERIOD_MS;
        if (s_kind == SENSOR_DS18B20 && probe_ready) delay_ms -= 800;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void sensor_start(void)
{
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 5, NULL);
}
