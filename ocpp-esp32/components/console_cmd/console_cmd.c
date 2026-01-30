#include "console_cmd.h"
#include "config_manager.h"
#include "led_status.h"
#include "gpio_control.h"
#include "ethernet_manager.h"
#include "wifi_manager.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "argtable3/argtable3.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "console";

/* ---- status ---- */
static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    const config_t *cfg = config_get();

    printf("=== OCPP ESP32 Status ===\n");
    printf("Device:     %s\n", cfg->dev_name);
    printf("Test mode:  %s\n", cfg->test_mode ? "ON" : "OFF");
    printf("Ethernet:   %s\n", ethernet_is_connected() ? "CONNECTED" : "disconnected");
    printf("WiFi STA:   %s\n", wifi_sta_is_connected() ? "CONNECTED" : "disconnected");
    printf("WiFi AP:    %s\n", wifi_ap_is_active() ? "ACTIVE" : "inactive");
    printf("Phase mode: %d-phase\n", (int)gpio_get_phase_mode());
    printf("Free heap:  %lu bytes\n",
           (unsigned long)esp_get_free_heap_size());
    printf("Min heap:   %lu bytes\n",
           (unsigned long)esp_get_minimum_free_heap_size());
    return 0;
}

/* ---- heap ---- */
static int cmd_heap(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Free heap:     %lu\n", (unsigned long)esp_get_free_heap_size());
    printf("Min free heap: %lu\n", (unsigned long)esp_get_minimum_free_heap_size());
    printf("Largest block: %lu\n",
           (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return 0;
}

/* ---- config show ---- */
static int cmd_config_show(int argc, char **argv)
{
    (void)argc; (void)argv;
    const config_t *cfg = config_get();

    printf("=== Configuration ===\n");
    printf("dev_name:     %s\n", cfg->dev_name);
    printf("test_mode:    %s\n", cfg->test_mode ? "true" : "false");
    printf("--- Ethernet ---\n");
    printf("eth_ip:       %s\n", cfg->eth_ip);
    printf("eth_subnet:   %s\n", cfg->eth_subnet);
    printf("eth_gw:       %s\n", cfg->eth_gw);
    printf("--- WiFi STA ---\n");
    printf("wifi_ssid:    %s\n", cfg->wifi_ssid);
    printf("wifi_pass:    %s\n", cfg->wifi_pass[0] ? "****" : "(empty)");
    printf("wifi_dhcp:    %s\n", cfg->wifi_dhcp ? "true" : "false");
    printf("wifi_ip:      %s\n", cfg->wifi_ip);
    printf("--- WiFi AP ---\n");
    printf("ap_ssid:      %s\n", cfg->ap_ssid);
    printf("ap_pass:      %s\n", cfg->ap_pass);
    printf("--- MQTT ---\n");
    printf("mqtt_host:    %s\n", cfg->mqtt_host);
    printf("mqtt_port:    %u\n", cfg->mqtt_port);
    printf("mqtt_user:    %s\n", cfg->mqtt_user);
    printf("mqtt_pass:    %s\n", cfg->mqtt_pass[0] ? "****" : "(empty)");
    printf("mqtt_prefix:  %s\n", cfg->mqtt_prefix);
    printf("mqtt_tls:     %s\n", cfg->mqtt_tls ? "true" : "false");
    printf("--- OCPP ---\n");
    printf("ws_port:      %u\n", cfg->ws_port);
    printf("hb_interval:  %u\n", cfg->hb_interval);
    printf("meter_intv:   %u\n", cfg->meter_interval);
    return 0;
}

/* ---- config set <key> <value> ---- */
static struct {
    struct arg_str *key;
    struct arg_str *value;
    struct arg_end *end;
} config_set_args;

static int cmd_config_set(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&config_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, config_set_args.end, argv[0]);
        return 1;
    }

    const char *key = config_set_args.key->sval[0];
    const char *val = config_set_args.value->sval[0];

    /* Try bool first */
    if (strcmp(val, "true") == 0 || strcmp(val, "false") == 0) {
        bool b = (strcmp(val, "true") == 0);
        if (config_set_bool(key, b) == ESP_OK) {
            printf("Set %s = %s (bool)\n", key, val);
            return 0;
        }
    }

    /* Try uint16 */
    char *endp;
    unsigned long num = strtoul(val, &endp, 10);
    if (*endp == '\0' && num <= 65535) {
        if (config_set_u16(key, (uint16_t)num) == ESP_OK) {
            printf("Set %s = %u (u16)\n", key, (unsigned)num);
            return 0;
        }
    }

    /* Try string */
    esp_err_t err = config_set_str(key, val);
    if (err == ESP_OK) {
        printf("Set %s = %s (str)\n", key, val);
        return 0;
    }

    printf("Unknown key: %s\n", key);
    return 1;
}

/* ---- factory reset ---- */
static int cmd_factory_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Performing factory reset...\n");
    config_factory_reset();
    printf("Factory defaults restored. Reboot to apply.\n");
    return 0;
}

/* ---- reboot ---- */
static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    esp_restart();
    return 0; /* unreachable */
}

/* ---- wifi scan ---- */
static int cmd_wifi_scan(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Scanning WiFi networks...\n");

    wifi_ap_record_t records[20];
    uint16_t count = wifi_scan(records, 20);

    printf("%-32s  %4s  %s\n", "SSID", "RSSI", "Auth");
    printf("%-32s  %4s  %s\n", "----", "----", "----");
    for (int i = 0; i < count; i++) {
        const char *auth;
        switch (records[i].authmode) {
        case WIFI_AUTH_OPEN:            auth = "OPEN"; break;
        case WIFI_AUTH_WPA_PSK:         auth = "WPA";  break;
        case WIFI_AUTH_WPA2_PSK:        auth = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK:    auth = "WPA/2";break;
        case WIFI_AUTH_WPA3_PSK:        auth = "WPA3"; break;
        case WIFI_AUTH_WPA2_WPA3_PSK:   auth = "WPA2/3";break;
        default:                        auth = "OTHER"; break;
        }
        printf("%-32s  %4d  %s\n", (char *)records[i].ssid,
               records[i].rssi, auth);
    }
    return 0;
}

/* ---- register all commands ---- */

static void register_commands(void)
{
    esp_console_register_help_command();

    const esp_console_cmd_t cmds[] = {
        {
            .command = "status",
            .help = "Show system status",
            .func = cmd_status,
        },
        {
            .command = "heap",
            .help = "Show heap memory info",
            .func = cmd_heap,
        },
        {
            .command = "config",
            .help = "Show all configuration",
            .hint = NULL,
            .func = cmd_config_show,
        },
        {
            .command = "factory_reset",
            .help = "Restore factory defaults",
            .func = cmd_factory_reset,
        },
        {
            .command = "reboot",
            .help = "Reboot the device",
            .func = cmd_reboot,
        },
        {
            .command = "wifi_scan",
            .help = "Scan for WiFi networks",
            .func = cmd_wifi_scan,
        },
    };

    for (int i = 0; i < (int)(sizeof(cmds) / sizeof(cmds[0])); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }

    /* "config_set" with argtable */
    config_set_args.key   = arg_str1(NULL, NULL, "<key>", "Config key name");
    config_set_args.value = arg_str1(NULL, NULL, "<value>", "Value to set");
    config_set_args.end   = arg_end(2);

    const esp_console_cmd_t set_cmd = {
        .command = "config_set",
        .help = "Set a config value: config_set <key> <value>",
        .func = cmd_config_set,
        .argtable = &config_set_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_cmd));
}

esp_err_t console_cmd_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "ocpp>";
    repl_cfg.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

    register_commands();

    ESP_LOGI(TAG, "Console REPL started");
    ESP_ERROR_CHECK(esp_console_start_repl(repl));

    return ESP_OK;
}
