#pragma once

#include "esp_err.h"
#include "gpio_control.h"
#include <stdbool.h>

/* Phase switch state machine states */
typedef enum {
    PHASE_SW_IDLE,
    PHASE_SW_STOPPING,
    PHASE_SW_WAITING,
    PHASE_SW_DELAY,
    PHASE_SW_SWITCHING,
    PHASE_SW_STARTING,
    PHASE_SW_ERROR,
} phase_switch_state_t;

/**
 * Initialise phase control module.
 * Must be called after ocpp_server and mqtt_manager are started.
 */
esp_err_t phase_control_init(void);

/**
 * Request a phase mode switch. The state machine handles the safety sequence.
 * Returns ESP_OK if the request was accepted (async operation).
 */
esp_err_t phase_control_request_switch(phase_mode_t target);

/**
 * Get the current phase switch state machine state.
 */
phase_switch_state_t phase_control_get_state(void);

/**
 * Get current effective phase mode.
 */
phase_mode_t phase_control_get_mode(void);

/**
 * Apply a power limit (kW). Automatically determines if phase switch is needed.
 * Called by MQTT command handler.
 */
esp_err_t phase_control_set_power_limit(float power_kw);

/**
 * Get the power correction factor for current phase mode.
 * Returns 1.0 for 3-phase, 3.0 for 1-phase (divide wallbox values by this).
 */
float phase_control_get_correction_factor(void);

/**
 * Notify phase control that connector status changed.
 * Used by OCPP status callback to advance state machine.
 */
void phase_control_notify_status(int connector_id, const char *status);

/**
 * Notify phase control that a transaction stopped.
 */
void phase_control_notify_transaction_stopped(void);
