#pragma once

#include "esp_err.h"

/**
 * Register all console commands and start the REPL task.
 * This function does not return (runs the REPL loop in its own task).
 */
esp_err_t console_cmd_start(void);
