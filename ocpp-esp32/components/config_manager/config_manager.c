#include "config_manager.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "config_mgr";
static const char *NVS_NS = "ocpp_cfg";

static config_t s_cfg;

/* ---------- defaults ---------- */

static void load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));

    strlcpy(s_cfg.dev_name,   "ocpp-esp32",     sizeof(s_cfg.dev_name));
    s_cfg.test_mode = false;

    strlcpy(s_cfg.eth_ip,     "192.168.4.1",    sizeof(s_cfg.eth_ip));
    strlcpy(s_cfg.eth_subnet, "255.255.255.0",  sizeof(s_cfg.eth_subnet));
    strlcpy(s_cfg.eth_gw,     "192.168.4.1",    sizeof(s_cfg.eth_gw));

    s_cfg.wifi_dhcp = true;

    /* AP SSID will be set with MAC suffix after NVS init */
    strlcpy(s_cfg.ap_pass,    "ocpp12345",      sizeof(s_cfg.ap_pass));

    s_cfg.mqtt_port     = 1883;
    strlcpy(s_cfg.mqtt_prefix, "ocpp",           sizeof(s_cfg.mqtt_prefix));
    s_cfg.mqtt_tls      = false;
    /* mqtt_client_id default set after MAC read */

    s_cfg.ws_port        = 9000;
    s_cfg.hb_interval    = 60;
    s_cfg.meter_interval = 30;
    strlcpy(s_cfg.auth_mode, "accept_all",       sizeof(s_cfg.auth_mode));

    s_cfg.ap_timeout            = 300;

    s_cfg.phase_switch_delay    = 5000;
    s_cfg.phase_1_max_current   = 160;   /* 16.0 A */
    s_cfg.phase_3_max_current   = 160;   /* 16.0 A */
    s_cfg.phase_switch_threshold = 4100; /* 4.1 kW in watts */

    s_cfg.ota_enabled           = true;
    s_cfg.ota_check_interval    = 0;     /* 0 = disabled */
    s_cfg.log_level             = 3;     /* ESP_LOG_INFO */
}

/* ---------- NVS helpers ---------- */

static nvs_handle_t s_nvs;

static void nvs_read_str(const char *key, char *dst, size_t dst_sz)
{
    size_t len = dst_sz;
    if (nvs_get_str(s_nvs, key, dst, &len) != ESP_OK) {
        /* keep default */
    }
}

static void nvs_read_u16(const char *key, uint16_t *dst)
{
    nvs_get_u16(s_nvs, key, dst);
}

static void nvs_read_bool(const char *key, bool *dst)
{
    uint8_t v;
    if (nvs_get_u8(s_nvs, key, &v) == ESP_OK) {
        *dst = (v != 0);
    }
}

static esp_err_t nvs_write_str(const char *key, const char *val)
{
    esp_err_t err = nvs_set_str(s_nvs, key, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

static esp_err_t nvs_write_u16(const char *key, uint16_t val)
{
    esp_err_t err = nvs_set_u16(s_nvs, key, val);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

static esp_err_t nvs_write_bool(const char *key, bool val)
{
    esp_err_t err = nvs_set_u8(s_nvs, key, val ? 1 : 0);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

/* ---------- public API ---------- */

esp_err_t config_manager_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    load_defaults();

    /* Build defaults from MAC */
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_cfg.ap_ssid, sizeof(s_cfg.ap_ssid),
             "OCPP-ESP32-%02X%02X", mac[4], mac[5]);
    snprintf(s_cfg.mqtt_client_id, sizeof(s_cfg.mqtt_client_id),
             "ocpp-esp32-%02X%02X", mac[4], mac[5]);

    err = nvs_open(NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Read persisted values (missing keys keep defaults) */
    nvs_read_str("dev_name",   s_cfg.dev_name,   sizeof(s_cfg.dev_name));
    nvs_read_bool("test_mode", &s_cfg.test_mode);

    nvs_read_str("eth_ip",     s_cfg.eth_ip,     sizeof(s_cfg.eth_ip));
    nvs_read_str("eth_subnet", s_cfg.eth_subnet, sizeof(s_cfg.eth_subnet));
    nvs_read_str("eth_gw",     s_cfg.eth_gw,     sizeof(s_cfg.eth_gw));

    nvs_read_str("wifi_ssid",  s_cfg.wifi_ssid,  sizeof(s_cfg.wifi_ssid));
    nvs_read_str("wifi_pass",  s_cfg.wifi_pass,  sizeof(s_cfg.wifi_pass));
    nvs_read_bool("wifi_dhcp", &s_cfg.wifi_dhcp);
    nvs_read_str("wifi_ip",    s_cfg.wifi_ip,    sizeof(s_cfg.wifi_ip));
    nvs_read_str("wifi_gw",    s_cfg.wifi_gw,    sizeof(s_cfg.wifi_gw));
    nvs_read_str("wifi_subnet",s_cfg.wifi_subnet,sizeof(s_cfg.wifi_subnet));
    nvs_read_str("wifi_dns",   s_cfg.wifi_dns,   sizeof(s_cfg.wifi_dns));

    nvs_read_str("ap_ssid",    s_cfg.ap_ssid,    sizeof(s_cfg.ap_ssid));
    nvs_read_str("ap_pass",    s_cfg.ap_pass,    sizeof(s_cfg.ap_pass));

    nvs_read_str("mqtt_host",  s_cfg.mqtt_host,  sizeof(s_cfg.mqtt_host));
    nvs_read_u16("mqtt_port",  &s_cfg.mqtt_port);
    nvs_read_str("mqtt_user",  s_cfg.mqtt_user,  sizeof(s_cfg.mqtt_user));
    nvs_read_str("mqtt_pass",  s_cfg.mqtt_pass,  sizeof(s_cfg.mqtt_pass));
    nvs_read_str("mqtt_prefix",s_cfg.mqtt_prefix,sizeof(s_cfg.mqtt_prefix));
    nvs_read_bool("mqtt_tls",  &s_cfg.mqtt_tls);

    nvs_read_str("mqtt_cid",   s_cfg.mqtt_client_id, sizeof(s_cfg.mqtt_client_id));

    nvs_read_u16("ws_port",    &s_cfg.ws_port);
    nvs_read_u16("hb_interval",&s_cfg.hb_interval);
    nvs_read_u16("meter_intv", &s_cfg.meter_interval);
    nvs_read_str("auth_mode",  s_cfg.auth_mode,  sizeof(s_cfg.auth_mode));

    nvs_read_u16("ap_timeout", &s_cfg.ap_timeout);

    nvs_read_u16("ph_delay",   &s_cfg.phase_switch_delay);
    nvs_read_u16("ph_1_max",   &s_cfg.phase_1_max_current);
    nvs_read_u16("ph_3_max",   &s_cfg.phase_3_max_current);
    nvs_read_u16("ph_thresh",  &s_cfg.phase_switch_threshold);

    nvs_read_bool("ota_en",    &s_cfg.ota_enabled);
    nvs_read_str("ota_url",    s_cfg.ota_url,    sizeof(s_cfg.ota_url));
    nvs_read_u16("ota_intv",   &s_cfg.ota_check_interval);

    {
        uint8_t lv;
        if (nvs_get_u8(s_nvs, "log_level", &lv) == ESP_OK) {
            s_cfg.log_level = lv;
        }
    }

    ESP_LOGI(TAG, "Config loaded: dev=%s wifi_ssid=%s eth_ip=%s ws_port=%u",
             s_cfg.dev_name, s_cfg.wifi_ssid, s_cfg.eth_ip, s_cfg.ws_port);

    return ESP_OK;
}

const config_t *config_get(void)
{
    return &s_cfg;
}

/* String key → field mapping */
typedef struct {
    const char *key;
    char *field;
    size_t sz;
} str_map_t;

static const str_map_t str_keys[] = {
    {"dev_name",    s_cfg.dev_name,    sizeof(s_cfg.dev_name)},
    {"eth_ip",      s_cfg.eth_ip,      sizeof(s_cfg.eth_ip)},
    {"eth_subnet",  s_cfg.eth_subnet,  sizeof(s_cfg.eth_subnet)},
    {"eth_gw",      s_cfg.eth_gw,      sizeof(s_cfg.eth_gw)},
    {"wifi_ssid",   s_cfg.wifi_ssid,   sizeof(s_cfg.wifi_ssid)},
    {"wifi_pass",   s_cfg.wifi_pass,   sizeof(s_cfg.wifi_pass)},
    {"wifi_ip",     s_cfg.wifi_ip,     sizeof(s_cfg.wifi_ip)},
    {"wifi_gw",     s_cfg.wifi_gw,     sizeof(s_cfg.wifi_gw)},
    {"wifi_subnet", s_cfg.wifi_subnet, sizeof(s_cfg.wifi_subnet)},
    {"wifi_dns",    s_cfg.wifi_dns,    sizeof(s_cfg.wifi_dns)},
    {"ap_ssid",     s_cfg.ap_ssid,     sizeof(s_cfg.ap_ssid)},
    {"ap_pass",     s_cfg.ap_pass,     sizeof(s_cfg.ap_pass)},
    {"mqtt_host",   s_cfg.mqtt_host,   sizeof(s_cfg.mqtt_host)},
    {"mqtt_user",   s_cfg.mqtt_user,   sizeof(s_cfg.mqtt_user)},
    {"mqtt_pass",   s_cfg.mqtt_pass,   sizeof(s_cfg.mqtt_pass)},
    {"mqtt_prefix", s_cfg.mqtt_prefix, sizeof(s_cfg.mqtt_prefix)},
    {"mqtt_cid",    s_cfg.mqtt_client_id, sizeof(s_cfg.mqtt_client_id)},
    {"auth_mode",   s_cfg.auth_mode,   sizeof(s_cfg.auth_mode)},
    {"ota_url",     s_cfg.ota_url,     sizeof(s_cfg.ota_url)},
    {NULL, NULL, 0}
};

esp_err_t config_set_str(const char *key, const char *value)
{
    for (const str_map_t *m = str_keys; m->key; m++) {
        if (strcmp(m->key, key) == 0) {
            strlcpy(m->field, value, m->sz);
            return nvs_write_str(key, value);
        }
    }
    ESP_LOGW(TAG, "Unknown string key: %s", key);
    return ESP_ERR_NOT_FOUND;
}

typedef struct {
    const char *key;
    uint16_t *field;
} u16_map_t;

static const u16_map_t u16_keys[] = {
    {"mqtt_port",    &s_cfg.mqtt_port},
    {"ws_port",      &s_cfg.ws_port},
    {"hb_interval",  &s_cfg.hb_interval},
    {"meter_intv",   &s_cfg.meter_interval},
    {"ap_timeout",   &s_cfg.ap_timeout},
    {"ph_delay",     &s_cfg.phase_switch_delay},
    {"ph_1_max",     &s_cfg.phase_1_max_current},
    {"ph_3_max",     &s_cfg.phase_3_max_current},
    {"ph_thresh",    &s_cfg.phase_switch_threshold},
    {"ota_intv",     &s_cfg.ota_check_interval},
    {NULL, NULL}
};

esp_err_t config_set_u16(const char *key, uint16_t value)
{
    /* log_level stored as u8 */
    if (strcmp(key, "log_level") == 0) {
        s_cfg.log_level = (uint8_t)value;
        esp_err_t err = nvs_set_u8(s_nvs, key, (uint8_t)value);
        if (err == ESP_OK) err = nvs_commit(s_nvs);
        return err;
    }
    for (const u16_map_t *m = u16_keys; m->key; m++) {
        if (strcmp(m->key, key) == 0) {
            *m->field = value;
            return nvs_write_u16(key, value);
        }
    }
    ESP_LOGW(TAG, "Unknown u16 key: %s", key);
    return ESP_ERR_NOT_FOUND;
}

typedef struct {
    const char *key;
    bool *field;
} bool_map_t;

static const bool_map_t bool_keys[] = {
    {"test_mode",  &s_cfg.test_mode},
    {"wifi_dhcp",  &s_cfg.wifi_dhcp},
    {"mqtt_tls",   &s_cfg.mqtt_tls},
    {"ota_en",     &s_cfg.ota_enabled},
    {NULL, NULL}
};

esp_err_t config_set_bool(const char *key, bool value)
{
    for (const bool_map_t *m = bool_keys; m->key; m++) {
        if (strcmp(m->key, key) == 0) {
            *m->field = value;
            return nvs_write_bool(key, value);
        }
    }
    ESP_LOGW(TAG, "Unknown bool key: %s", key);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t config_factory_reset(void)
{
    ESP_LOGW(TAG, "Factory reset: erasing NVS namespace");
    esp_err_t err = nvs_erase_all(s_nvs);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
        return err;
    }
    load_defaults();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_cfg.ap_ssid, sizeof(s_cfg.ap_ssid),
             "OCPP-ESP32-%02X%02X", mac[4], mac[5]);

    ESP_LOGI(TAG, "Factory defaults restored");
    return ESP_OK;
}

/* ---------- JSON serialization ---------- */

cJSON *config_get_json(void)
{
    const config_t *cfg = &s_cfg;
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    /* Device */
    cJSON_AddStringToObject(root, "dev_name",    cfg->dev_name);
    cJSON_AddBoolToObject(root,   "test_mode",   cfg->test_mode);

    /* Ethernet */
    cJSON_AddStringToObject(root, "eth_ip",      cfg->eth_ip);
    cJSON_AddStringToObject(root, "eth_subnet",  cfg->eth_subnet);
    cJSON_AddStringToObject(root, "eth_gw",      cfg->eth_gw);

    /* WiFi STA */
    cJSON_AddStringToObject(root, "wifi_ssid",   cfg->wifi_ssid);
    cJSON_AddStringToObject(root, "wifi_pass",   cfg->wifi_pass);
    cJSON_AddBoolToObject(root,   "wifi_dhcp",   cfg->wifi_dhcp);
    cJSON_AddStringToObject(root, "wifi_ip",     cfg->wifi_ip);
    cJSON_AddStringToObject(root, "wifi_gw",     cfg->wifi_gw);
    cJSON_AddStringToObject(root, "wifi_subnet", cfg->wifi_subnet);
    cJSON_AddStringToObject(root, "wifi_dns",    cfg->wifi_dns);

    /* WiFi AP */
    cJSON_AddStringToObject(root, "ap_ssid",     cfg->ap_ssid);
    cJSON_AddStringToObject(root, "ap_pass",     cfg->ap_pass);
    cJSON_AddNumberToObject(root, "ap_timeout",  cfg->ap_timeout);

    /* MQTT */
    cJSON_AddStringToObject(root, "mqtt_host",   cfg->mqtt_host);
    cJSON_AddNumberToObject(root, "mqtt_port",   cfg->mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_user",   cfg->mqtt_user);
    cJSON_AddStringToObject(root, "mqtt_pass",   cfg->mqtt_pass);
    cJSON_AddStringToObject(root, "mqtt_prefix", cfg->mqtt_prefix);
    cJSON_AddBoolToObject(root,   "mqtt_tls",    cfg->mqtt_tls);
    cJSON_AddStringToObject(root, "mqtt_cid",    cfg->mqtt_client_id);

    /* OCPP / WebSocket */
    cJSON_AddNumberToObject(root, "ws_port",     cfg->ws_port);
    cJSON_AddNumberToObject(root, "hb_interval", cfg->hb_interval);
    cJSON_AddNumberToObject(root, "meter_intv",  cfg->meter_interval);
    cJSON_AddStringToObject(root, "auth_mode",   cfg->auth_mode);

    /* Phase switching */
    cJSON_AddNumberToObject(root, "ph_delay",    cfg->phase_switch_delay);
    cJSON_AddNumberToObject(root, "ph_1_max",    cfg->phase_1_max_current);
    cJSON_AddNumberToObject(root, "ph_3_max",    cfg->phase_3_max_current);
    cJSON_AddNumberToObject(root, "ph_thresh",   cfg->phase_switch_threshold);

    /* OTA */
    cJSON_AddBoolToObject(root,   "ota_en",      cfg->ota_enabled);
    cJSON_AddStringToObject(root, "ota_url",     cfg->ota_url);
    cJSON_AddNumberToObject(root, "ota_intv",    cfg->ota_check_interval);

    /* System */
    cJSON_AddNumberToObject(root, "log_level",   cfg->log_level);

    return root;
}

esp_err_t config_set_from_json(cJSON *root)
{
    if (!root) return ESP_ERR_INVALID_ARG;

    /* String keys */
    for (const str_map_t *m = str_keys; m->key; m++) {
        cJSON *item = cJSON_GetObjectItem(root, m->key);
        if (item && cJSON_IsString(item)) {
            config_set_str(m->key, item->valuestring);
        }
    }

    /* u16 keys */
    for (const u16_map_t *m = u16_keys; m->key; m++) {
        cJSON *item = cJSON_GetObjectItem(root, m->key);
        if (item && cJSON_IsNumber(item)) {
            config_set_u16(m->key, (uint16_t)item->valuedouble);
        }
    }

    /* log_level (stored as u8, handled by config_set_u16) */
    cJSON *ll = cJSON_GetObjectItem(root, "log_level");
    if (ll && cJSON_IsNumber(ll)) {
        config_set_u16("log_level", (uint16_t)ll->valuedouble);
    }

    /* Bool keys */
    for (const bool_map_t *m = bool_keys; m->key; m++) {
        cJSON *item = cJSON_GetObjectItem(root, m->key);
        if (item && cJSON_IsBool(item)) {
            config_set_bool(m->key, cJSON_IsTrue(item));
        }
    }

    return ESP_OK;
}
