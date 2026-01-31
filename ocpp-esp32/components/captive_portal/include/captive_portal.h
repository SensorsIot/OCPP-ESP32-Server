#pragma once

#include "esp_err.h"

/**
 * Start the captive portal (HTTP server + DNS redirect).
 * Must be called after wifi_manager_init(WIFI_OP_AP_ONLY).
 */
esp_err_t captive_portal_start(void);

/**
 * Stop the captive portal and free resources.
 */
void captive_portal_stop(void);
