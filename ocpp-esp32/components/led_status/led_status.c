#include "led_status.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "led_status";

/* Map led_id_t → GPIO */
static const gpio_num_t led_gpio[LED_ID_MAX] = {
    [LED_ID_STATUS] = PIN_LED_STATUS,
    [LED_ID_OCPP]   = PIN_LED_OCPP,
    [LED_ID_WIFI]   = PIN_LED_WIFI,
    [LED_ID_MQTT]   = PIN_LED_MQTT,
};

typedef struct {
    led_pattern_t pattern;
    uint16_t tick;           /* 50ms ticks since pattern start */
} led_state_t;

static led_state_t s_leds[LED_ID_MAX];
static esp_timer_handle_t s_timer;

/* Tick period in microseconds (50ms = 20 Hz) */
#define TICK_US  (50 * 1000)

/* Tick counts for each cadence (1 tick = 50ms) */
#define SLOW_HALF   10   /* 500ms */
#define SLOW_PERIOD 20   /* 1000ms */
#define FAST_HALF    2   /* 100ms */
#define FAST_PERIOD  4   /* 200ms */
#define DOUBLE_ON1   2   /* first flash 100ms */
#define DOUBLE_GAP1  4   /* gap after first flash */
#define DOUBLE_ON2   6   /* second flash start */
#define DOUBLE_GAP2  8   /* gap after second flash */
#define DOUBLE_PERIOD 20 /* total cycle 1s */
#define TRAFFIC_ON    2  /* 100ms flash */
#define TRAFFIC_PERIOD 2 /* auto-returns to previous */

static bool pattern_level(led_pattern_t pat, uint16_t tick)
{
    switch (pat) {
    case LED_PATTERN_OFF:
        return false;
    case LED_PATTERN_SOLID:
        return true;
    case LED_PATTERN_SLOW_BLINK: {
        uint16_t t = tick % SLOW_PERIOD;
        return t < SLOW_HALF;
    }
    case LED_PATTERN_FAST_BLINK: {
        uint16_t t = tick % FAST_PERIOD;
        return t < FAST_HALF;
    }
    case LED_PATTERN_DOUBLE: {
        uint16_t t = tick % DOUBLE_PERIOD;
        return (t < DOUBLE_ON1) || (t >= DOUBLE_GAP1 && t < DOUBLE_ON2);
    }
    case LED_PATTERN_TRAFFIC: {
        return tick < TRAFFIC_ON;
    }
    default:
        return false;
    }
}

static void timer_cb(void *arg)
{
    (void)arg;
    for (int i = 0; i < LED_ID_MAX; i++) {
        led_state_t *ls = &s_leds[i];
        bool level = pattern_level(ls->pattern, ls->tick);
        gpio_set_level(led_gpio[i], level ? 1 : 0);
        ls->tick++;

        /* Auto-revert traffic pattern after the flash */
        if (ls->pattern == LED_PATTERN_TRAFFIC && ls->tick >= TRAFFIC_PERIOD) {
            ls->pattern = LED_PATTERN_OFF;
            ls->tick = 0;
        }
    }
}

esp_err_t led_status_init(void)
{
    memset(s_leds, 0, sizeof(s_leds));

    for (int i = 0; i < LED_ID_MAX; i++) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << led_gpio[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure GPIO %d: %s",
                     led_gpio[i], esp_err_to_name(err));
            return err;
        }
        gpio_set_level(led_gpio[i], 0);
    }

    const esp_timer_create_args_t timer_args = {
        .callback = timer_cb,
        .name = "led_tick",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer create failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_timer_start_periodic(s_timer, TICK_US);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer start failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LED status initialised (4 LEDs, 50ms tick)");
    return ESP_OK;
}

void led_status_set(led_id_t led, led_pattern_t pattern)
{
    if (led >= LED_ID_MAX) return;
    s_leds[led].pattern = pattern;
    s_leds[led].tick = 0;
}

led_pattern_t led_status_get(led_id_t led)
{
    if (led >= LED_ID_MAX) return LED_PATTERN_OFF;
    return s_leds[led].pattern;
}
