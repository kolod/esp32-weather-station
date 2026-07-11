#pragma once

/** @brief Initialize ST7789V SPI panel, LVGL port, backlight, and start the
 *         display FreeRTOS task (prio 4, 6 KB stack). */
void display_start(void);
