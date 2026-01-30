#pragma once

#include "esp_err.h"

typedef enum {
    LED_ID_STATUS = 0,
    LED_ID_OCPP,
    LED_ID_WIFI,
    LED_ID_MQTT,
    LED_ID_MAX
} led_id_t;

typedef enum {
    LED_PATTERN_OFF = 0,    /* Steady off */
    LED_PATTERN_SOLID,      /* Steady on */
    LED_PATTERN_SLOW_BLINK, /* 1 Hz  (500ms on / 500ms off) */
    LED_PATTERN_FAST_BLINK, /* 5 Hz  (100ms on / 100ms off) */
    LED_PATTERN_DOUBLE,     /* Double flash then pause (OTA) */
    LED_PATTERN_TRAFFIC,    /* Single short flash (activity) */
} led_pattern_t;

/**
 * Initialise LED GPIOs and start the 50ms tick timer.
 */
esp_err_t led_status_init(void);

/**
 * Set the pattern for a given LED.
 */
void led_status_set(led_id_t led, led_pattern_t pattern);

/**
 * Get the current pattern for a given LED.
 */
led_pattern_t led_status_get(led_id_t led);
