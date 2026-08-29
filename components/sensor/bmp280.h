#pragma once
/**
 * @file bmp280.h
 * @brief Minimal Bosch BMP280 / BME280 driver over ESP-IDF i2c_master
 *        (feature 005: BMP280; feature 007: BME280 + humidity).
 *
 * Protocol per Bosch datasheets BST-BMP280-DS001 / BST-BME280-DS002:
 * chip-id probe (0xD0 == 0x58 BMP280, 0x60 BME280), 24-byte factory
 * calibration at 0x88 (plus 0xA1 + 0xE1..0xE7 humidity calibration on a
 * BME280), normal-mode configuration, burst measurement read at 0xF7
 * (6 bytes BMP280, 8 bytes BME280), integer compensation.
 *
 * Temperature and pressure compensation are byte-identical between the two
 * parts and share t_fine. The compensation functions are pure and exposed
 * for unit tests; the datasheet worked examples are the reference vectors.
 */
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/** Factory calibration coefficients.
 *  T/P: registers 0x88..0x9F, little-endian (both parts).
 *  H:   register 0xA1 + 0xE1..0xE7 (BME280 only; zero on a BMP280). */
typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} bmp280_calib_t;

typedef struct {
    i2c_master_dev_handle_t dev;
    bmp280_calib_t          calib;
    uint8_t                 addr;         /* 0x76 or 0x77 */
    bool                    has_humidity; /* true: BME280 (chip-id 0x60) */
} bmp280_t;

/**
 * @brief Probe addresses 0x76 then 0x77 for a BMP280 (chip-id 0x58) or
 *        BME280 (chip-id 0x60) and read its calibration data. When both
 *        addresses hold a Bosch sensor, a BME280 is preferred over a BMP280.
 * @return ESP_OK and a filled-in handle (check @ref bmp280_t::has_humidity),
 *         or ESP_ERR_NOT_FOUND.
 */
esp_err_t bmp280_detect(i2c_master_bus_handle_t bus, bmp280_t *out);

/**
 * @brief Configure normal mode: osrs_t x2, osrs_p x16, IIR filter 4,
 *        standby 1000 ms (research D3). On a BME280, osrs_h x1 is written to
 *        ctrl_hum first (required for it to take effect on the next ctrl_meas).
 */
esp_err_t bmp280_configure(bmp280_t *dev);

/**
 * @brief Burst-read the latest measurement and compensate.
 * @param temp_c    out: temperature in °C
 * @param press_hpa out: station pressure in hPa
 * @param hum_pct   out: relative humidity in %RH; may be NULL. Left untouched
 *                  when the device is a BMP280 (no humidity channel).
 */
esp_err_t bmp280_read(bmp280_t *dev, float *temp_c, float *press_hpa, float *hum_pct);

/** @brief Release the I2C device handle. Safe on a partially set-up handle. */
void bmp280_release(bmp280_t *dev);

/* ── Pure compensation (datasheet integer algorithms; exposed for tests) ── */

/** @return temperature in 0.01 °C; also yields t_fine for pressure/humidity. */
int32_t bmp280_compensate_t(int32_t raw_t, const bmp280_calib_t *c, int32_t *t_fine);

/** @return pressure in Pa as unsigned Q24.8 (divide by 256 for Pa). */
uint32_t bmp280_compensate_p(int32_t raw_p, const bmp280_calib_t *c, int32_t t_fine);

/** @brief BME280 humidity compensation (BST-BME280-DS002 §4.2.3,
 *         BME280_compensate_H_int32).
 *  @return relative humidity as unsigned Q22.10 (divide by 1024 for %RH);
 *          clamped to the range [0, 100 %RH] (0 .. 102400). */
uint32_t bme280_compensate_h(int32_t raw_h, const bmp280_calib_t *c, int32_t t_fine);
