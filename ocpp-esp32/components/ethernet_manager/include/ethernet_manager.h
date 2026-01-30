#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialise W5500 SPI Ethernet with static IP from config.
 * Requires esp_netif_init() and esp_event_loop_create_default() done already.
 */
esp_err_t ethernet_manager_init(void);

/**
 * Returns true when Ethernet link is up and IP is assigned.
 */
bool ethernet_is_connected(void);
