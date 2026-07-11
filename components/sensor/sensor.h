#pragma once

/**
 * @brief Start the sensor FreeRTOS task.
 *        Initializes the DS18B20 probe on GPIO27 (1-Wire RMT) and begins
 *        reading every 5 seconds. Updates app_state.reading and posts
 *        APP_EVT_READING_UPDATED on each read cycle.
 */
void sensor_start(void);
