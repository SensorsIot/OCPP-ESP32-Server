#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define CFG_STR_MAX  64
#define CFG_IP_MAX   16

typedef struct {
    /* Device */
    char     dev_name[CFG_STR_MAX];   /* NVS: dev_name */
    bool     test_mode;               /* NVS: test_mode */

    /* Ethernet (wallbox side) */
    char     eth_ip[CFG_IP_MAX];      /* NVS: eth_ip */
    char     eth_subnet[CFG_IP_MAX];  /* NVS: eth_subnet */
    char     eth_gw[CFG_IP_MAX];      /* NVS: eth_gw */

    /* WiFi STA */
    char     wifi_ssid[CFG_STR_MAX];  /* NVS: wifi_ssid */
    char     wifi_pass[CFG_STR_MAX];  /* NVS: wifi_pass */
    bool     wifi_dhcp;               /* NVS: wifi_dhcp */
    char     wifi_ip[CFG_IP_MAX];     /* NVS: wifi_ip */
    char     wifi_gw[CFG_IP_MAX];     /* NVS: wifi_gw */
    char     wifi_subnet[CFG_IP_MAX]; /* NVS: wifi_subnet */
    char     wifi_dns[CFG_IP_MAX];    /* NVS: wifi_dns */

    /* WiFi AP */
    char     ap_ssid[CFG_STR_MAX];    /* NVS: ap_ssid */
    char     ap_pass[CFG_STR_MAX];    /* NVS: ap_pass */

    /* MQTT */
    char     mqtt_host[CFG_STR_MAX];  /* NVS: mqtt_host */
    uint16_t mqtt_port;               /* NVS: mqtt_port */
    char     mqtt_user[CFG_STR_MAX];  /* NVS: mqtt_user */
    char     mqtt_pass[CFG_STR_MAX];  /* NVS: mqtt_pass */
    char     mqtt_prefix[CFG_STR_MAX];/* NVS: mqtt_prefix */
    bool     mqtt_tls;                /* NVS: mqtt_tls */

    /* OCPP / WebSocket */
    uint16_t ws_port;                 /* NVS: ws_port */
    uint16_t hb_interval;             /* NVS: hb_interval */
    uint16_t meter_interval;          /* NVS: meter_intv */
} config_t;

/**
 * Initialise NVS and load configuration.
 * Must be called before any other config_manager function.
 */
esp_err_t config_manager_init(void);

/**
 * Get pointer to the current (in-RAM) configuration.
 * Returned pointer is valid for the lifetime of the application.
 */
const config_t *config_get(void);

/**
 * Set a string config value by key name and persist to NVS.
 * Returns ESP_ERR_NOT_FOUND if key is unknown.
 */
esp_err_t config_set_str(const char *key, const char *value);

/**
 * Set a uint16 config value by key name and persist to NVS.
 */
esp_err_t config_set_u16(const char *key, uint16_t value);

/**
 * Set a bool config value by key name and persist to NVS.
 */
esp_err_t config_set_bool(const char *key, bool value);

/**
 * Erase all NVS config and reload defaults.
 */
esp_err_t config_factory_reset(void);
