#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    WIFI_OP_STA,      /* Station only (normal mode) */
    WIFI_OP_AP_ONLY,  /* Access point only (config mode) */
    WIFI_OP_STA_AP,   /* Concurrent STA + AP (fallback mode) */
} wifi_op_mode_t;

/**
 * Initialise WiFi in the requested mode.
 * Requires esp_netif_init() and esp_event_loop_create_default() done already.
 */
esp_err_t wifi_manager_init(wifi_op_mode_t mode);

/**
 * Returns true when WiFi STA is connected and has an IP.
 */
bool wifi_sta_is_connected(void);

/**
 * Returns true when WiFi AP is active.
 */
bool wifi_ap_is_active(void);

/**
 * Scan for nearby access points (blocking).
 * Results are written to `out`, up to `max_count` entries.
 * Returns actual count found.
 */
uint16_t wifi_scan(wifi_ap_record_t *out, uint16_t max_count);
