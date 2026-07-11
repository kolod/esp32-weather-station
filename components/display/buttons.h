#pragma once

/** @brief Initialize left (GPIO0) and right (GPIO35) buttons.
 *         - Left click: toggle time mode (local↔UTC)
 *         - Right click: toggle temperature unit (°C↔°F)
 *         - Left long-press (5 s): factory reset */
void buttons_init(void);
