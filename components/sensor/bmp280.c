#include "bmp280.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define TAG            "bmp280"

#define REG_CALIB      0x88
#define REG_CALIB_H1   0xA1
#define REG_CALIB_H2   0xE1 /* 0xE1..0xE7: dig_H2..dig_H6 */
#define REG_CHIP_ID    0xD0
#define REG_CTRL_HUM   0xF2
#define REG_CTRL_MEAS  0xF4
#define REG_CONFIG     0xF5
#define REG_DATA       0xF7 /* press msb,lsb,xlsb, temp msb,lsb,xlsb[, hum msb,lsb] */

#define CHIP_ID_BMP280 0x58
#define CHIP_ID_BME280 0x60
#define I2C_TIMEOUT_MS 100

/* osrs_t x2 (010) | osrs_p x16 (101) | mode normal (11) */
#define CTRL_MEAS_VAL  ((2 << 5) | (5 << 2) | 3)
/* t_sb 1000 ms (101) | IIR filter 4 (010) | spi3w off */
#define CONFIG_VAL     ((5 << 5) | (2 << 2) | 0)
/* osrs_h x1 (001) — BME280 only, written before ctrl_meas */
#define CTRL_HUM_VAL   0x01

/* ── Pure compensation — Bosch datasheets, integer versions ── */

int32_t bmp280_compensate_t(int32_t raw_t, const bmp280_calib_t *c, int32_t *t_fine)
{
    int32_t var1 = ((((raw_t >> 3) - ((int32_t)c->dig_T1 << 1))) *
                    ((int32_t)c->dig_T2)) >> 11;
    int32_t var2 = (((((raw_t >> 4) - ((int32_t)c->dig_T1)) *
                      ((raw_t >> 4) - ((int32_t)c->dig_T1))) >> 12) *
                    ((int32_t)c->dig_T3)) >> 14;
    *t_fine = var1 + var2;
    return (*t_fine * 5 + 128) >> 8; /* 0.01 °C */
}

uint32_t bmp280_compensate_p(int32_t raw_p, const bmp280_calib_t *c, int32_t t_fine)
{
    int64_t var1 = (int64_t)t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)c->dig_P6;
    var2 += (var1 * (int64_t)c->dig_P5) << 17;
    var2 += ((int64_t)c->dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)c->dig_P3) >> 8) +
           ((var1 * (int64_t)c->dig_P2) << 12);
    var1 = ((((int64_t)1 << 47) + var1) * (int64_t)c->dig_P1) >> 33;
    if (var1 == 0) return 0; /* avoid division by zero */

    int64_t p = 1048576 - raw_p;
    p = ((p << 31) - var2) * 3125 / var1;
    var1 = ((int64_t)c->dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->dig_P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)c->dig_P7 << 4);
    return (uint32_t)p; /* Pa, Q24.8 */
}

uint32_t bme280_compensate_h(int32_t raw_h, const bmp280_calib_t *c, int32_t t_fine)
{
    int32_t v = t_fine - (int32_t)76800;
    v = (((((raw_h << 14) - ((int32_t)c->dig_H4 << 20) - ((int32_t)c->dig_H5 * v)) +
           (int32_t)16384) >> 15) *
         (((((((v * (int32_t)c->dig_H6) >> 10) *
              (((v * (int32_t)c->dig_H3) >> 11) + (int32_t)32768)) >> 10) +
            (int32_t)2097152) * (int32_t)c->dig_H2 + 8192) >> 14));
    v = v - (((((v >> 15) * (v >> 15)) >> 7) * (int32_t)c->dig_H1) >> 4);
    if (v < 0) v = 0;
    if (v > 419430400) v = 419430400;
    return (uint32_t)(v >> 12); /* Q22.10: %RH = value / 1024 */
}

/* ── Register access ── */

static esp_err_t reg_read(bmp280_t *d, uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(d->dev, &reg, 1, buf, len, I2C_TIMEOUT_MS);
}

static esp_err_t reg_write(bmp280_t *d, uint8_t reg, uint8_t val)
{
    uint8_t frame[2] = {reg, val};
    return i2c_master_transmit(d->dev, frame, sizeof(frame), I2C_TIMEOUT_MS);
}

static void parse_calib(const uint8_t b[24], bmp280_calib_t *c)
{
    c->dig_T1 = (uint16_t)(b[0]  | b[1]  << 8);
    c->dig_T2 = (int16_t)(b[2]   | b[3]  << 8);
    c->dig_T3 = (int16_t)(b[4]   | b[5]  << 8);
    c->dig_P1 = (uint16_t)(b[6]  | b[7]  << 8);
    c->dig_P2 = (int16_t)(b[8]   | b[9]  << 8);
    c->dig_P3 = (int16_t)(b[10]  | b[11] << 8);
    c->dig_P4 = (int16_t)(b[12]  | b[13] << 8);
    c->dig_P5 = (int16_t)(b[14]  | b[15] << 8);
    c->dig_P6 = (int16_t)(b[16]  | b[17] << 8);
    c->dig_P7 = (int16_t)(b[18]  | b[19] << 8);
    c->dig_P8 = (int16_t)(b[20]  | b[21] << 8);
    c->dig_P9 = (int16_t)(b[22]  | b[23] << 8);
}

/* BME280 humidity calibration: dig_H1 @ 0xA1, dig_H2..dig_H6 packed in 0xE1..0xE7.
   Layout per BST-BME280-DS002 Table 16 (e[] indexes 0xE1..0xE7). */
static esp_err_t read_humidity_calib(bmp280_t *d)
{
    uint8_t h1 = 0, e[7] = {0};
    esp_err_t err = reg_read(d, REG_CALIB_H1, &h1, 1);
    if (err != ESP_OK) return err;
    err = reg_read(d, REG_CALIB_H2, e, sizeof(e));
    if (err != ESP_OK) return err;

    d->calib.dig_H1 = h1;
    d->calib.dig_H2 = (int16_t)(e[0] | (e[1] << 8));
    d->calib.dig_H3 = e[2];
    d->calib.dig_H4 = (int16_t)(((int16_t)(int8_t)e[3] << 4) | (e[4] & 0x0F));
    d->calib.dig_H5 = (int16_t)(((int16_t)(int8_t)e[5] << 4) | (e[4] >> 4));
    d->calib.dig_H6 = (int8_t)e[6];
    return ESP_OK;
}

/* Scan the whole 7-bit address range (0x08..0x77) and log every device that
 * ACKs, so a missing sensor can be told apart from a wiring problem or a
 * sensor strapped to an unexpected address. */
static void log_bus_scan(i2c_master_bus_handle_t bus)
{
    char found[5 * (0x77 - 0x08 + 1) + 1]; /* " 0xNN" per address, worst case */
    size_t pos = 0;
    int count = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus, addr, I2C_TIMEOUT_MS) != ESP_OK)
            continue;
        pos += snprintf(found + pos, sizeof(found) - pos, " 0x%02X", addr);
        count++;
    }

    if (count > 0)
        ESP_LOGW(TAG, "I2C bus scan: %d device(s) ACKed:%s", count, found);
    else
        ESP_LOGW(TAG, "I2C bus scan: no devices ACKed (check wiring/pull-ups)");
}

/* ── Public API ── */

void bmp280_release(bmp280_t *d)
{
    if (d && d->dev) {
        i2c_master_bus_rm_device(d->dev);
        d->dev = NULL;
    }
}

/* Set up one candidate at `addr`: add device, verify chip-id, read calibration.
   On success `cand` is fully populated. On failure the device handle is removed
   and ESP_ERR_NOT_FOUND / the underlying error is returned. */
static esp_err_t try_address(i2c_master_bus_handle_t bus, uint8_t addr, bmp280_t *cand)
{
    if (i2c_master_probe(bus, addr, I2C_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "No ACK at 0x%02X", addr);
        return ESP_ERR_NOT_FOUND;
    }

    memset(cand, 0, sizeof(*cand));
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 400000,
    };
    if (i2c_master_bus_add_device(bus, &dev_cfg, &cand->dev) != ESP_OK)
        return ESP_ERR_NOT_FOUND;

    uint8_t chip_id = 0;
    esp_err_t err = reg_read(cand, REG_CHIP_ID, &chip_id, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Chip ID read failed at 0x%02X (%s)", addr, esp_err_to_name(err));
        bmp280_release(cand);
        return err;
    }
    if (chip_id != CHIP_ID_BMP280 && chip_id != CHIP_ID_BME280) {
        ESP_LOGW(TAG, "Device at 0x%02X is not a BMP280/BME280 (id=0x%02X)", addr, chip_id);
        bmp280_release(cand);
        return ESP_ERR_NOT_FOUND;
    }

    cand->addr         = addr;
    cand->has_humidity = (chip_id == CHIP_ID_BME280);

    uint8_t raw[24];
    if (reg_read(cand, REG_CALIB, raw, sizeof(raw)) != ESP_OK) {
        ESP_LOGW(TAG, "Calibration read failed at 0x%02X", addr);
        bmp280_release(cand);
        return ESP_ERR_NOT_FOUND;
    }
    parse_calib(raw, &cand->calib);

    if (cand->has_humidity && read_humidity_calib(cand) != ESP_OK) {
        ESP_LOGW(TAG, "Humidity calibration read failed at 0x%02X", addr);
        bmp280_release(cand);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "%s found at 0x%02X", cand->has_humidity ? "BME280" : "BMP280", addr);
    return ESP_OK;
}

esp_err_t bmp280_detect(i2c_master_bus_handle_t bus, bmp280_t *out)
{
    static const uint8_t addrs[] = {0x76, 0x77};
    bmp280_t bmp_fallback = {0};
    bool     have_fallback = false;

    for (size_t i = 0; i < sizeof(addrs); i++) {
        bmp280_t cand;
        if (try_address(bus, addrs[i], &cand) != ESP_OK)
            continue;

        if (cand.has_humidity) {
            /* Richest sensor wins immediately; drop any BMP280 fallback. */
            if (have_fallback) bmp280_release(&bmp_fallback);
            *out = cand;
            return ESP_OK;
        }
        if (have_fallback)
            bmp280_release(&cand); /* already hold a BMP280 */
        else {
            bmp_fallback  = cand;
            have_fallback = true;
        }
    }

    if (have_fallback) {
        *out = bmp_fallback;
        return ESP_OK;
    }

    ESP_LOGW(TAG, "No BMP280/BME280 found at 0x76 or 0x77");
    log_bus_scan(bus);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bmp280_configure(bmp280_t *d)
{
    esp_err_t err = reg_write(d, REG_CONFIG, CONFIG_VAL);
    if (err == ESP_OK && d->has_humidity)
        err = reg_write(d, REG_CTRL_HUM, CTRL_HUM_VAL); /* must precede ctrl_meas */
    if (err == ESP_OK)
        err = reg_write(d, REG_CTRL_MEAS, CTRL_MEAS_VAL);
    return err;
}

esp_err_t bmp280_read(bmp280_t *d, float *temp_c, float *press_hpa, float *hum_pct)
{
    uint8_t b[8];
    size_t len = d->has_humidity ? 8 : 6;
    esp_err_t err = reg_read(d, REG_DATA, b, len);
    if (err != ESP_OK) return err;

    int32_t raw_p = (int32_t)(((uint32_t)b[0] << 12) | ((uint32_t)b[1] << 4) | (b[2] >> 4));
    int32_t raw_t = (int32_t)(((uint32_t)b[3] << 12) | ((uint32_t)b[4] << 4) | (b[5] >> 4));

    /* All-zero xlsb pattern 0x80000 means "no measurement yet" */
    if (raw_t == 0x80000 || raw_p == 0x80000) return ESP_ERR_INVALID_STATE;

    int32_t t_fine;
    int32_t t_centi = bmp280_compensate_t(raw_t, &d->calib, &t_fine);
    uint32_t p_q248 = bmp280_compensate_p(raw_p, &d->calib, t_fine);
    if (p_q248 == 0) return ESP_ERR_INVALID_STATE;

    *temp_c    = (float)t_centi / 100.0f;
    *press_hpa = (float)p_q248 / 256.0f / 100.0f; /* Q24.8 Pa → hPa */

    if (d->has_humidity && hum_pct) {
        int32_t raw_h = (int32_t)(((uint32_t)b[6] << 8) | b[7]);
        *hum_pct = (float)bme280_compensate_h(raw_h, &d->calib, t_fine) / 1024.0f;
    }
    return ESP_OK;
}
