#include "ethernet_manager.h"
#include "board_pins.h"
#include "led_status.h"

#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_mac.h"

#include <string.h>

static const char *TAG = "eth_mgr";
static bool s_connected;
static esp_eth_handle_t s_eth_handle;
static esp_netif_t *s_eth_netif;

static void eth_event_handler(void *arg, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    (void)arg;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        led_status_set(LED_ID_OCPP, LED_PATTERN_SLOW_BLINK);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Ethernet link down");
        s_connected = false;
        led_status_set(LED_ID_OCPP, LED_PATTERN_OFF);
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        s_connected = false;
        led_status_set(LED_ID_OCPP, LED_PATTERN_OFF);
        break;
    default:
        break;
    }
}

static void got_ip_handler(void *arg, esp_event_base_t base,
                           int32_t event_id, void *event_data)
{
    (void)arg;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    if (event->esp_netif == s_eth_netif) {
        ESP_LOGI(TAG, "Ethernet IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        led_status_set(LED_ID_OCPP, LED_PATTERN_SOLID);
    }
}

esp_err_t ethernet_manager_init(void)
{
    ESP_LOGI(TAG, "Initializing Ethernet MAC for WT32-ETH01...");

    /* Internal EMAC configuration for RMII */
    eth_esp32_emac_config_t emac_cfg = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac_cfg.smi_mdc_gpio_num = PIN_ETH_MDC;
    emac_cfg.smi_mdio_gpio_num = PIN_ETH_MDIO;
    emac_cfg.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_cfg.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO;  /* GPIO0 */

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    mac_cfg.sw_reset_timeout_ms = 1000;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_cfg, &mac_cfg);
    if (!mac) {
        ESP_LOGE(TAG, "EMAC creation failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initializing Ethernet PHY (LAN8720A)...");

    /* LAN8720 PHY */
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.phy_addr = ETH_PHY_ADDR;
    phy_cfg.reset_gpio_num = -1;  /* No hardware reset pin on WT32-ETH01 */

    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_cfg);
    if (!phy) {
        ESP_LOGE(TAG, "LAN8720 PHY creation failed");
        return ESP_FAIL;
    }

    /* Enable external oscillator BEFORE driver install
     * GPIO16 is pulled down at boot to allow IO0 strapping */
    ESP_ERROR_CHECK(gpio_set_direction(PIN_ETH_PWR, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(PIN_ETH_PWR, 1));
    vTaskDelay(pdMS_TO_TICKS(500));  /* Allow oscillator to stabilize */

    ESP_LOGI(TAG, "Starting Ethernet interface...");

    /* Ethernet driver */
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&eth_cfg, &s_eth_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Keep auto-negotiation enabled (forced speeds didn't help) */

    /* Set MAC address from base MAC */
    uint8_t eth_mac_addr[6];
    esp_read_mac(eth_mac_addr, ESP_MAC_ETH);
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr));

    /* Network interface */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);
    esp_eth_netif_glue_handle_t eth_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, eth_netif_glue));

    /* DHCP enabled by default */

    /* Event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               got_ip_handler, NULL));

    /* Start */
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));

    ESP_LOGI(TAG, "WT32-ETH01 Ethernet initialised (LAN8720 RMII, DHCP)");
    return ESP_OK;
}

bool ethernet_is_connected(void)
{
    return s_connected;
}
