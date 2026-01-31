#include "led_status.h"

#include "esp_log.h"

static const char *TAG = "led_status";

static led_pattern_t s_patterns[LED_ID_MAX];

esp_err_t led_status_init(void)
{
    ESP_LOGI(TAG, "LED status disabled (no LED hardware)");
    return ESP_OK;
}

void led_status_set(led_id_t led, led_pattern_t pattern)
{
    if (led < LED_ID_MAX) {
        s_patterns[led] = pattern;
    }
}

led_pattern_t led_status_get(led_id_t led)
{
    if (led >= LED_ID_MAX) return LED_PATTERN_OFF;
    return s_patterns[led];
}
