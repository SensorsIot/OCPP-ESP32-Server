#include "wifi_manager.h"
#include "config_manager.h"
#include "led_status.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"

#include <string.h>

static const char *TAG = "wifi_mgr";

static bool s_sta_connected;
static bool s_ap_active;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;

/* Reconnect backoff state */
static esp_timer_handle_t s_reconnect_timer;
static uint32_t s_backoff_ms = 1000;
#define BACKOFF_MAX_MS  30000

static void attempt_reconnect(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "WiFi STA reconnect attempt (backoff %"PRIu32" ms)", s_backoff_ms);
    esp_wifi_connect();
}

static void schedule_reconnect(void)
{
    esp_timer_start_once(s_reconnect_timer, (uint64_t)s_backoff_ms * 1000);
    s_backoff_ms *= 2;
    if (s_backoff_ms > BACKOFF_MAX_MS) {
        s_backoff_ms = BACKOFF_MAX_MS;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started, connecting...");
            led_status_set(LED_ID_WIFI, LED_PATTERN_SLOW_BLINK);
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "STA disconnected");
            s_sta_connected = false;
            led_status_set(LED_ID_WIFI, LED_PATTERN_SLOW_BLINK);
            schedule_reconnect();
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP started");
            s_ap_active = true;
            led_status_set(LED_ID_WIFI, LED_PATTERN_FAST_BLINK);
            break;
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG, "AP stopped");
            s_ap_active = false;
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "AP client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "AP client disconnected");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *ev = event_data;
            ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&ev->ip_info.ip));
            s_sta_connected = true;
            s_backoff_ms = 1000;
            led_status_set(LED_ID_WIFI, LED_PATTERN_SOLID);
        }
    }
}

static void set_ap_ip(esp_netif_t *netif)
{
    esp_netif_dhcps_stop(netif);
    esp_netif_ip_info_t ap_ip = {0};
    IP4_ADDR(&ap_ip.ip, 192, 168, 1, 1);
    IP4_ADDR(&ap_ip.gw, 192, 168, 1, 1);
    IP4_ADDR(&ap_ip.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(netif, &ap_ip);
    esp_netif_dhcps_start(netif);
}

esp_err_t wifi_manager_init(wifi_op_mode_t mode)
{
    const config_t *cfg = config_get();

    /* Create reconnect timer */
    const esp_timer_create_args_t timer_args = {
        .callback = attempt_reconnect,
        .name = "wifi_reconn",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    /* Init WiFi with default config */
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               wifi_event_handler, NULL));

    switch (mode) {
    case WIFI_OP_STA: {
        ESP_LOGI(TAG, "Mode: STA (ssid=%s)", cfg->wifi_ssid);
        s_sta_netif = esp_netif_create_default_wifi_sta();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t sta_cfg = {0};
        strlcpy((char *)sta_cfg.sta.ssid, cfg->wifi_ssid, sizeof(sta_cfg.sta.ssid));
        strlcpy((char *)sta_cfg.sta.password, cfg->wifi_pass, sizeof(sta_cfg.sta.password));
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));

        if (!cfg->wifi_dhcp && cfg->wifi_ip[0]) {
            esp_netif_dhcpc_stop(s_sta_netif);
            esp_netif_ip_info_t ip = {0};
            esp_netif_str_to_ip4(cfg->wifi_ip, &ip.ip);
            esp_netif_str_to_ip4(cfg->wifi_gw, &ip.gw);
            esp_netif_str_to_ip4(cfg->wifi_subnet, &ip.netmask);
            esp_netif_set_ip_info(s_sta_netif, &ip);
            if (cfg->wifi_dns[0]) {
                esp_netif_dns_info_t dns = {0};
                esp_netif_str_to_ip4(cfg->wifi_dns, &dns.ip.u_addr.ip4);
                dns.ip.type = ESP_IPADDR_TYPE_V4;
                esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
            }
        }
        break;
    }
    case WIFI_OP_AP_ONLY: {
        ESP_LOGI(TAG, "Mode: AP only (ssid=%s, open)", cfg->ap_ssid);
        s_ap_netif = esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

        wifi_config_t ap_cfg = {
            .ap = {
                .max_connection = 4,
                .authmode = WIFI_AUTH_OPEN,
                .channel = 1,
            },
        };
        strlcpy((char *)ap_cfg.ap.ssid, cfg->ap_ssid, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len = strlen(cfg->ap_ssid);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

        set_ap_ip(s_ap_netif);
        break;
    }
    case WIFI_OP_STA_AP: {
        ESP_LOGI(TAG, "Mode: STA+AP (sta=%s ap=%s)", cfg->wifi_ssid, cfg->ap_ssid);
        s_sta_netif = esp_netif_create_default_wifi_sta();
        s_ap_netif  = esp_netif_create_default_wifi_ap();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

        wifi_config_t stacfg = {0};
        strlcpy((char *)stacfg.sta.ssid, cfg->wifi_ssid, sizeof(stacfg.sta.ssid));
        strlcpy((char *)stacfg.sta.password, cfg->wifi_pass, sizeof(stacfg.sta.password));
        stacfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &stacfg));

        wifi_config_t apcfg = {
            .ap = {
                .max_connection = 4,
                .authmode = WIFI_AUTH_WPA2_PSK,
                .channel = 1,
            },
        };
        strlcpy((char *)apcfg.ap.ssid, cfg->ap_ssid, sizeof(apcfg.ap.ssid));
        apcfg.ap.ssid_len = strlen(cfg->ap_ssid);
        strlcpy((char *)apcfg.ap.password, cfg->ap_pass, sizeof(apcfg.ap.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &apcfg));

        set_ap_ip(s_ap_netif);
        break;
    }
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialised");
    return ESP_OK;
}

bool wifi_sta_is_connected(void)
{
    return s_sta_connected;
}

bool wifi_ap_is_active(void)
{
    return s_ap_active;
}

uint16_t wifi_scan(wifi_ap_record_t *out, uint16_t max_count)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t count = max_count;
    esp_wifi_scan_get_ap_records(&count, out);

    ESP_LOGI(TAG, "WiFi scan found %u networks", count);
    return count;
}
