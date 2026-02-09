#include "gpio_control.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "gpio_ctrl";

static phase_mode_t s_phase_mode = PHASE_MODE_1;
static gpio_config_btn_cb_t s_btn_cb;
static esp_timer_handle_t s_btn_timer;

/* Button hold detection: poll every 100ms, trigger at 5s hold */
#define BTN_POLL_US       (100 * 1000)
#define BTN_HOLD_TICKS    50   /* 50 * 100ms = 5s */

static uint16_t s_btn_hold_count;
static bool     s_btn_triggered;

static void btn_poll_cb(void *arg)
{
    (void)arg;
    bool pressed = (gpio_get_level(PIN_BTN_CONFIG) == 0); /* active low */

    if (pressed) {
        s_btn_hold_count++;
        if (s_btn_hold_count >= BTN_HOLD_TICKS && !s_btn_triggered) {
            s_btn_triggered = true;
            ESP_LOGI(TAG, "Config button held 5s — triggering callback");
            if (s_btn_cb) {
                s_btn_cb();
            }
        }
    } else {
        s_btn_hold_count = 0;
        s_btn_triggered = false;
    }
}

esp_err_t gpio_control_init(gpio_config_btn_cb_t config_btn_cb)
{
    s_btn_cb = config_btn_cb;

    /* Single relay output for L2+L3 — default OFF (1-phase) */
    gpio_config_t relay_io = {
        .pin_bit_mask = 1ULL << PIN_RELAY_PHASE23,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&relay_io));
    gpio_set_level(PIN_RELAY_PHASE23, 0);
    s_phase_mode = PHASE_MODE_1;

    /* Config button input (GPIO 0 has external pull-up on most dev boards) */
    gpio_config_t btn_io = {
        .pin_bit_mask = 1ULL << PIN_BTN_CONFIG,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_io));

    /* Button polling timer */
    const esp_timer_create_args_t timer_args = {
        .callback = btn_poll_cb,
        .name = "btn_poll",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_btn_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_btn_timer, BTN_POLL_US));

    ESP_LOGI(TAG, "GPIO control initialised (1-phase default, single relay for L2+L3)");
    return ESP_OK;
}

esp_err_t gpio_set_phase_mode(phase_mode_t mode)
{
    if (mode == PHASE_MODE_3) {
        gpio_set_level(PIN_RELAY_PHASE23, 1);
        s_phase_mode = PHASE_MODE_3;
        ESP_LOGI(TAG, "Phase mode set to 3-phase (L2+L3 relay ON)");
    } else {
        gpio_set_level(PIN_RELAY_PHASE23, 0);
        s_phase_mode = PHASE_MODE_1;
        ESP_LOGI(TAG, "Phase mode set to 1-phase (L2+L3 relay OFF)");
    }
    return ESP_OK;
}

phase_mode_t gpio_get_phase_mode(void)
{
    return s_phase_mode;
}

