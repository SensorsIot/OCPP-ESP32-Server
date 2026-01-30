#include "led_status.h"
#include "config_manager.h"
#include "gpio_control.h"
#include "ethernet_manager.h"
#include "wifi_manager.h"
#include "console_cmd.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void on_config_button(void)
{
    ESP_LOGW(TAG, "Config button held — entering config mode");
    led_status_set(LED_ID_STATUS, LED_PATTERN_SLOW_BLINK);
    /* In a full implementation this would start AP mode and captive portal.
       For Phase 1 we just log the event. */
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== OCPP ESP32 Server starting ===");

    /* 1. LED status — fast blink while booting */
    ESP_ERROR_CHECK(led_status_init());
    led_status_set(LED_ID_STATUS, LED_PATTERN_FAST_BLINK);

    /* 2. Configuration from NVS */
    ESP_ERROR_CHECK(config_manager_init());

    /* 3. GPIO: relays (default 1-phase), config button */
    ESP_ERROR_CHECK(gpio_control_init(on_config_button));

    /* 4. Network stack (called once, shared by ETH + WiFi) */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 5. Decide boot mode based on WiFi config */
    const config_t *cfg = config_get();

    if (cfg->wifi_ssid[0] == '\0') {
        /* No WiFi configured → Config mode (AP only) */
        ESP_LOGW(TAG, "No WiFi SSID configured — starting in CONFIG mode");
        led_status_set(LED_ID_STATUS, LED_PATTERN_SLOW_BLINK);
        ESP_ERROR_CHECK(wifi_manager_init(WIFI_OP_AP_ONLY));
    } else {
        /* Normal mode: Ethernet + WiFi STA */
        ESP_LOGI(TAG, "Starting in NORMAL mode");
        ESP_ERROR_CHECK(ethernet_manager_init());
        ESP_ERROR_CHECK(wifi_manager_init(WIFI_OP_STA));
    }

    /* 6. Serial console REPL */
    ESP_ERROR_CHECK(console_cmd_start());

    /* 7. Boot complete — solid status LED */
    led_status_set(LED_ID_STATUS, LED_PATTERN_SOLID);

    /* 8. Subscribe main task to watchdog */
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    ESP_LOGI(TAG, "=== OCPP ESP32 Server ready ===");

    /* Main loop: feed watchdog periodically */
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
