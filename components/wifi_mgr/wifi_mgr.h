#pragma once

#include <stdint.h>
#include <stddef.h>
#include "app_ctx.h"

/**
 * SoftAP IPv4 address (also the DNS-redirect / captive-portal target).
 * Not 192.168.4.1 — that's the ESP-IDF default and collides with some
 * consumer routers, which breaks captive-portal detection when a client
 * is dual-homed (e.g. already has a router on 192.168.4.1 via another link).
 * Keep the dotted-quad octets and string form in sync if this changes.
 */
#define WIFI_MGR_AP_IP_1   192
#define WIFI_MGR_AP_IP_2   168
#define WIFI_MGR_AP_IP_3   16
#define WIFI_MGR_AP_IP_4   1
#define WIFI_MGR_AP_IP_STR "192.168.16.1"

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

/**
 * Current WiFi-reachability facts, as shown on the display's WiFi details screen.
 * All fields are always NUL-terminated after wifi_mgr_get_info().
 */
typedef struct {
    char ssid[33];      /* joined SSID (STA) or broadcast SSID (AP fallback); "" if unknown */
    char ipv4[16];      /* dotted-quad IPv4 of the active interface; "" if no IP yet         */
    char hostname[32];  /* "weather-XXXX.local" — always populated                          */
} wifi_mgr_info_t;

/**
 * @brief Snapshot the current WiFi-reachability facts into *out.
 *
 * Non-blocking, safe from any task and at any time (before or after wifi_mgr_start()).
 * Chooses STA-interface values when connected, SoftAP-interface values in AP fallback,
 * and stored/empty values while connecting. On any internal getter failure the affected
 * field is left as "" (still valid; the display renders it as a placeholder).
 *
 * @param out  caller-owned struct; fully overwritten (zeroed first). NULL is ignored.
 */
void wifi_mgr_get_info(wifi_mgr_info_t *out);
