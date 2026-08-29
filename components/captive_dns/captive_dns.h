#pragma once
#include "esp_err.h"

/** @brief Start UDP DNS server on port 53 that answers all A-queries with the SoftAP IP (WIFI_MGR_AP_IP_STR). */
esp_err_t captive_dns_start(void);

/** @brief Stop the captive DNS server. */
void captive_dns_stop(void);
