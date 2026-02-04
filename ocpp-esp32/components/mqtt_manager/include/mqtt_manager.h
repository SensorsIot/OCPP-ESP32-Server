#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * Start MQTT client. Connects to broker configured in config_manager.
 * Subscribes to command topics and publishes status.
 * Must be called after WiFi STA is connected.
 */
esp_err_t mqtt_manager_start(void);

/**
 * Stop the MQTT client.
 */
void mqtt_manager_stop(void);

/**
 * Returns true if MQTT is connected.
 */
bool mqtt_is_connected(void);

/**
 * Publish a JSON string to a topic under the configured prefix.
 * topic: sub-topic (e.g. "status", "meter", "phase")
 */
esp_err_t mqtt_publish(const char *topic, const char *json_str, int qos);

/**
 * Publish phase switch result.
 */
esp_err_t mqtt_publish_phase_result(bool success, const char *old_mode,
                                     const char *new_mode, int duration_ms,
                                     const char *error);

/**
 * Publish current phase status.
 */
esp_err_t mqtt_publish_phase_status(const char *phase_mode, float correction_factor);
