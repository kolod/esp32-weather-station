#pragma once
#include "esp_err.h"

/**
 * @brief Start the HTTPS management server on :443.
 *        Loads device certificate and key from /storage/certs/.
 *        Returns ESP_ERR_NOT_FOUND if certificate is not provisioned.
 */
esp_err_t mgmt_server_start(void);

/** @brief Stop the HTTPS management server. */
void mgmt_server_stop(void);
