#pragma once

/**
 * @brief Start the sensor FreeRTOS task.
 *
 * Detects the fitted sensor once at startup (feature 005/007):
 *   1. BME280 on I2C (SCL=GPIO22, SDA=GPIO21, addr 0x76/0x77, chip-id 0x60)
 *      → supplies temperature, pressure AND humidity;
 *   2. otherwise BMP280 on the same bus (chip-id 0x58)
 *      → supplies temperature and pressure; humidity stays invalid;
 *   3. otherwise DS18B20 probe on GPIO27 (1-Wire RMT)
 *      → temperature only, pressure and humidity stay invalid;
 *   4. otherwise all readings stay invalid.
 *
 * When both a BME280 and a BMP280 answer on the bus, the BME280 wins.
 * The choice is fixed until reboot; runtime failures invalidate readings and
 * retry the SAME sensor, never switching sources mid-run. Reads every
 * 5 seconds, updates app_state.reading / .pressure / .humidity /
 * .sensor_kind, and posts APP_EVT_READING_UPDATED per cycle.
 */
void sensor_start(void);
