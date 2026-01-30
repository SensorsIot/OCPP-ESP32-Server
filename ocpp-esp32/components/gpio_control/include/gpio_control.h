#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    PHASE_MODE_1 = 1,
    PHASE_MODE_3 = 3,
} phase_mode_t;

/**
 * Callback invoked when CONFIG button is held for 5 seconds.
 */
typedef void (*gpio_config_btn_cb_t)(void);

/**
 * Initialise relay outputs (default: 1-phase), phase sense input,
 * and config button polling timer.
 */
esp_err_t gpio_control_init(gpio_config_btn_cb_t config_btn_cb);

/**
 * Set phase relay outputs.
 * CALLER is responsible for ensuring no active charging session.
 */
esp_err_t gpio_set_phase_mode(phase_mode_t mode);

/**
 * Read current relay output state.
 */
phase_mode_t gpio_get_phase_mode(void);

/**
 * Read the PHASE_SENSE feedback input.
 * Returns true if feedback indicates 3-phase connected.
 */
bool gpio_read_phase_sense(void);
