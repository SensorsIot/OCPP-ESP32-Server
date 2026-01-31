#include "led_status.h"
#include "config_manager.h"
#include "gpio_control.h"
#include "ethernet_manager.h"
#include "wifi_manager.h"
#include "captive_portal.h"
#include "ocpp_server.h"
#include "mqtt_manager.h"
#include "phase_control.h"
#include "console_cmd.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_sntp.h"
#include "nvs_flash.h"

static const char *TAG = "main";

/* ---------- NTP time sync ---------- */

static void init_ntp(void)
{
    ESP_LOGI(TAG, "Initialising NTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
}

/* ---------- Config mode ---------- */

static void start_config_mode(void)
{
    ESP_LOGW(TAG, "Starting CONFIG mode with captive portal");
    led_status_set(LED_ID_STATUS, LED_PATTERN_SLOW_BLINK);
    ESP_ERROR_CHECK(wifi_manager_init(WIFI_OP_AP_ONLY));
    ESP_ERROR_CHECK(captive_portal_start());
}

static void on_config_button(void)
{
    ESP_LOGW(TAG, "Config button held — entering config mode");
    captive_portal_start();
    led_status_set(LED_ID_STATUS, LED_PATTERN_SLOW_BLINK);
}

/* ---------- Normal mode ---------- */

static void start_normal_mode(void)
{
    const config_t *cfg = config_get();

    ESP_LOGI(TAG, "Starting in NORMAL mode");

    /* Ethernet for wallbox OCPP */
    ESP_ERROR_CHECK(ethernet_manager_init());

    /* WiFi STA for MQTT */
    ESP_ERROR_CHECK(wifi_manager_init(WIFI_OP_STA));

    /* NTP time sync over WiFi */
    init_ntp();

    /* OCPP WebSocket server (listens on all interfaces in test mode,
       otherwise primarily on Ethernet) */
    ESP_ERROR_CHECK(ocpp_server_start());

    /* MQTT client (starts when WiFi connects) */
    if (cfg->mqtt_host[0] != '\0') {
        /* Delay MQTT start slightly to allow WiFi to connect */
        ESP_LOGI(TAG, "MQTT will start after WiFi connects");
        /* mqtt_manager_start() is safe to call — it handles no-connection gracefully */
        mqtt_manager_start();
    }

    /* Phase switching control */
    ESP_ERROR_CHECK(phase_control_init());
}

/* ---------- Main ---------- */

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
        /* No WiFi configured → Config mode (AP + captive portal) */
        start_config_mode();
    } else {
        /* Normal mode: full stack */
        start_normal_mode();
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
