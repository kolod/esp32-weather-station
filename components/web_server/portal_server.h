#pragma once
#include "esp_err.h"

/** @brief Start the HTTP portal server on :80 (AP mode). */
esp_err_t portal_server_start(void);

/** @brief Stop the HTTP portal server. */
void portal_server_stop(void);
