#pragma once

/** @brief Initialize left (GPIO0) and right (GPIO35) buttons.
 *         - Left click: toggle time mode (local↔UTC)
 *         - Right click: toggle temperature unit (°C↔°F)
 *         - Left long-press: factory reset
 *         - Right long-press: show/hide the WiFi details screen
 *           (network name, IP address, local hostname); auto-hides after 10 s */
void buttons_init(void);
