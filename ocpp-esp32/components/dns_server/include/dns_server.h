#pragma once

#include "esp_err.h"

/**
 * Start the captive-portal DNS server.
 * All A-record queries are answered with the given IP (e.g. "192.168.1.1").
 * Runs as a FreeRTOS task until dns_server_stop() is called.
 */
esp_err_t dns_server_start(const char *redirect_ip);

/**
 * Stop the DNS server task and free resources.
 */
void dns_server_stop(void);
