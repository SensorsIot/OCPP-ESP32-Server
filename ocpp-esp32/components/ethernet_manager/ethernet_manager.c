#include "ethernet_manager.h"
#include "board_pins.h"
#include "config_manager.h"
#include "led_status.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
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
    const config_t *cfg = config_get();

    /* SPI bus configuration */
    spi_bus_config_t buscfg = {
        .miso_io_num   = PIN_ETH_MISO,
        .mosi_io_num   = PIN_ETH_MOSI,
        .sclk_io_num   = PIN_ETH_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t err = spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* SPI device config for W5500 (device allocated internally by driver) */
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = PIN_ETH_CS,
        .queue_size = 20,
    };

    /* W5500 MAC — v5.4 API takes SPI host + device config pointer */
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &devcfg);
    w5500_cfg.int_gpio_num = PIN_ETH_INT;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);
    if (!mac) {
        ESP_LOGE(TAG, "W5500 MAC creation failed");
        return ESP_FAIL;
    }

    /* W5500 PHY */
    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = PIN_ETH_RST;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);
    if (!phy) {
        ESP_LOGE(TAG, "W5500 PHY creation failed");
        return ESP_FAIL;
    }

    /* Ethernet driver */
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth_handle));

    /* Set MAC address from base MAC */
    uint8_t eth_mac_addr[6];
    esp_read_mac(eth_mac_addr, ESP_MAC_ETH);
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, eth_mac_addr));

    /* Network interface */
    esp_netif_inherent_config_t netif_base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t netif_cfg = {
        .base = &netif_base,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_eth_netif = esp_netif_new(&netif_cfg);
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));

    /* Static IP */
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_eth_netif));

    esp_netif_ip_info_t ip_info = {0};
    esp_netif_str_to_ip4(cfg->eth_ip, &ip_info.ip);
    esp_netif_str_to_ip4(cfg->eth_subnet, &ip_info.netmask);
    esp_netif_str_to_ip4(cfg->eth_gw, &ip_info.gw);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_eth_netif, &ip_info));

    /* Event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               got_ip_handler, NULL));

    /* Start */
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));

    ESP_LOGI(TAG, "Ethernet initialised: IP=%s SPI@%dMHz",
             cfg->eth_ip, ETH_SPI_CLOCK_MHZ);
    return ESP_OK;
}

bool ethernet_is_connected(void)
{
    return s_connected;
}
