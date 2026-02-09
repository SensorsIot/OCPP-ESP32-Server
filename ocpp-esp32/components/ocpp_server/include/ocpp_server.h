#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>

/* OCPP connector status */
typedef enum {
    OCPP_STATUS_AVAILABLE,
    OCPP_STATUS_PREPARING,
    OCPP_STATUS_CHARGING,
    OCPP_STATUS_SUSPENDED_EV,
    OCPP_STATUS_SUSPENDED_EVSE,
    OCPP_STATUS_FINISHING,
    OCPP_STATUS_RESERVED,
    OCPP_STATUS_UNAVAILABLE,
    OCPP_STATUS_FAULTED,
} ocpp_status_t;

/* Session/transaction state */
typedef struct {
    bool     active;
    int      transaction_id;
    char     id_tag[21];
    int      connector_id;
    int32_t  meter_start;
    int32_t  meter_current;
    int32_t  power_w;             /* current power in watts */
    float    current_a;           /* current in amps */
    float    voltage_l1;          /* L1 voltage (V), 0 if not reported */
    float    voltage_l2;          /* L2 voltage (V), 0 if not reported */
    float    voltage_l3;          /* L3 voltage (V), 0 if not reported */
    int64_t  start_time;          /* unix timestamp ms */
    ocpp_status_t status;
    char     charge_point_id[64];
    bool     connected;
} ocpp_session_t;

/* Callback for status changes — MQTT bridge subscribes to this */
typedef void (*ocpp_status_cb_t)(int connector_id, ocpp_status_t status,
                                  const char *error_code);
typedef void (*ocpp_session_cb_t)(const ocpp_session_t *session, bool started);
typedef void (*ocpp_meter_cb_t)(int connector_id, int transaction_id,
                                 const cJSON *values);

/**
 * Start the OCPP WebSocket server on the configured port.
 * Must be called after network init.
 */
esp_err_t ocpp_server_start(void);

/**
 * Stop the OCPP server.
 */
void ocpp_server_stop(void);

/**
 * Register callbacks for MQTT bridge integration.
 */
void ocpp_server_set_status_cb(ocpp_status_cb_t cb);
void ocpp_server_set_session_cb(ocpp_session_cb_t cb);
void ocpp_server_set_meter_cb(ocpp_meter_cb_t cb);

/**
 * Get current session state.
 */
const ocpp_session_t *ocpp_server_get_session(void);

/**
 * Send a remote command to the connected charge point.
 * Returns ESP_OK if message was queued.
 */
esp_err_t ocpp_send_remote_start(int connector_id, const char *id_tag);
esp_err_t ocpp_send_remote_stop(int transaction_id);
esp_err_t ocpp_send_change_availability(int connector_id, const char *type);
esp_err_t ocpp_send_reset(const char *type);
esp_err_t ocpp_send_unlock_connector(int connector_id);
esp_err_t ocpp_send_trigger_message(const char *requested_message);
esp_err_t ocpp_send_set_charging_profile(int connector_id, const cJSON *profile);
esp_err_t ocpp_send_clear_charging_profile(int id, int connector_id,
                                            const char *purpose, int stack_level);
esp_err_t ocpp_send_get_configuration(const char *key);
esp_err_t ocpp_send_change_configuration(const char *key, const char *value);
esp_err_t ocpp_send_update_firmware(const char *location, const char *retrieve_date,
                                     int retries, int retry_interval);
esp_err_t ocpp_send_get_diagnostics(const char *location, const char *start_time,
                                     const char *stop_time, int retries, int retry_interval);

/**
 * Get connector status string from enum.
 */
const char *ocpp_status_str(ocpp_status_t status);
