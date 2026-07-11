#pragma once

#include <stdint.h>
#include <stddef.h>
#include "app_ctx.h"

/**
 * @brief Initialize WiFi, read stored credentials, connect or start AP fallback.
 *        Also starts mDNS and SNTP on STA connect, and manages reconnect retry loop.
 *        This function starts an internal event-handler task; call once from main.
 */
void wifi_mgr_start(void);

/**
 * @brief Format the last 2 bytes of the default ESP32 MAC as 4 lowercase hex chars.
 * @param buf   caller-supplied buffer, must be >= 5 bytes
 */
void mac_to_suffix(char *buf);

/**
 * @brief Attempt to join the stored STA network (called from portal after credential save).
 *        Non-blocking; result delivered via WIFI_STATE_CHANGED events.
 */
void wifi_mgr_connect_sta(void);

/**
 * @brief Get current WiFi state snapshot.
 */
wifi_state_t wifi_mgr_get_state(void);
