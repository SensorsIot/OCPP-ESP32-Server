#include "captive_portal.h"
#include "dns_server.h"
#include "ota_manager.h"
#include "config_manager.h"
#include "wifi_manager.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "cJSON.h"

#include <string.h>

static const char *TAG = "portal";

#define PORTAL_IP       "192.168.1.1"
#define MAX_POST_SIZE   2048

static httpd_handle_t s_server;

/* Embedded HTML — linked by CMake EMBED_FILES */
extern const uint8_t portal_html_start[] asm("_binary_portal_html_start");
extern const uint8_t portal_html_end[]   asm("_binary_portal_html_end");

/* ---------- helpers ---------- */

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON encode failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

static cJSON *recv_json(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > MAX_POST_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body too large or empty");
        return NULL;
    }
    char *buf = malloc(total + 1);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return NULL;
    }
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Recv timeout");
            return NULL;
        }
        received += ret;
    }
    buf[total] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    }
    return root;
}

/* ---------- HTTP handlers ---------- */

static esp_err_t handler_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    size_t len = portal_html_end - portal_html_start;
    httpd_resp_send(req, (const char *)portal_html_start, len);
    return ESP_OK;
}

static esp_err_t handler_api_config_get(httpd_req_t *req)
{
    cJSON *root = config_get_json();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON build failed");
        return ESP_FAIL;
    }
    return send_json(req, root);
}

static esp_err_t handler_api_config_post(httpd_req_t *req)
{
    cJSON *root = recv_json(req);
    if (!root) return ESP_FAIL;

    config_set_from_json(root);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    return send_json(req, resp);
}

static esp_err_t handler_api_wifi_scan(httpd_req_t *req)
{
    wifi_ap_record_t records[20];
    uint16_t count = wifi_scan(records, 20);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", (const char *)records[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", records[i].rssi);
        cJSON_AddNumberToObject(ap, "channel", records[i].primary);
        cJSON_AddNumberToObject(ap, "authmode", records[i].authmode);

        const char *auth_str = "unknown";
        switch (records[i].authmode) {
        case WIFI_AUTH_OPEN:           auth_str = "open"; break;
        case WIFI_AUTH_WEP:            auth_str = "WEP"; break;
        case WIFI_AUTH_WPA_PSK:        auth_str = "WPA"; break;
        case WIFI_AUTH_WPA2_PSK:       auth_str = "WPA2"; break;
        case WIFI_AUTH_WPA_WPA2_PSK:   auth_str = "WPA/WPA2"; break;
        case WIFI_AUTH_WPA3_PSK:       auth_str = "WPA3"; break;
        case WIFI_AUTH_WPA2_WPA3_PSK:  auth_str = "WPA2/WPA3"; break;
        default: break;
        }
        cJSON_AddStringToObject(ap, "auth", auth_str);

        cJSON_AddItemToArray(arr, ap);
    }
    return send_json(req, arr);
}

static esp_err_t handler_api_system_status(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    cJSON_AddNumberToObject(root, "heap_free", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "heap_min", (double)esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(root, "uptime_ms", (double)(esp_timer_get_time() / 1000));
    cJSON_AddStringToObject(root, "idf_version", esp_get_idf_version());

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    cJSON_AddNumberToObject(root, "chip_cores", chip.cores);
    cJSON_AddNumberToObject(root, "chip_revision", chip.revision);

    return send_json(req, root);
}

static void reboot_timer_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

static esp_err_t handler_api_system_reboot(httpd_req_t *req)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "msg", "Rebooting in 500ms");
    send_json(req, resp);

    /* Delayed reboot so the HTTP response can be sent */
    const esp_timer_create_args_t timer_args = {
        .callback = reboot_timer_cb,
        .name = "reboot_tmr",
    };
    esp_timer_handle_t tmr;
    esp_timer_create(&timer_args, &tmr);
    esp_timer_start_once(tmr, 500 * 1000);

    return ESP_OK;
}

static esp_err_t handler_api_factory_reset(httpd_req_t *req)
{
    config_factory_reset();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "msg", "Factory reset done, rebooting in 500ms");
    send_json(req, resp);

    const esp_timer_create_args_t timer_args = {
        .callback = reboot_timer_cb,
        .name = "rst_reboot",
    };
    esp_timer_handle_t tmr;
    esp_timer_create(&timer_args, &tmr);
    esp_timer_start_once(tmr, 500 * 1000);

    return ESP_OK;
}

/* Captive portal detection endpoints */
static esp_err_t handler_generate_204(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.1.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t handler_hotspot_detect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.1.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Catch-all: redirect to root */
static esp_err_t handler_catch_all(httpd_req_t *req)
{
    /* Don't redirect API calls */
    if (strncmp(req->uri, "/api/", 5) == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");
        return ESP_FAIL;
    }
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.1.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---------- URI definitions ---------- */

static const httpd_uri_t uri_root = {
    .uri = "/", .method = HTTP_GET, .handler = handler_root,
};
static const httpd_uri_t uri_config_get = {
    .uri = "/api/config", .method = HTTP_GET, .handler = handler_api_config_get,
};
static const httpd_uri_t uri_config_post = {
    .uri = "/api/config", .method = HTTP_POST, .handler = handler_api_config_post,
};
static const httpd_uri_t uri_wifi_scan = {
    .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = handler_api_wifi_scan,
};
static const httpd_uri_t uri_system_status = {
    .uri = "/api/system/status", .method = HTTP_GET, .handler = handler_api_system_status,
};
static const httpd_uri_t uri_system_reboot = {
    .uri = "/api/system/reboot", .method = HTTP_POST, .handler = handler_api_system_reboot,
};
static const httpd_uri_t uri_factory_reset = {
    .uri = "/api/system/factory-reset", .method = HTTP_POST, .handler = handler_api_factory_reset,
};
static const httpd_uri_t uri_generate_204 = {
    .uri = "/generate_204", .method = HTTP_GET, .handler = handler_generate_204,
};
static const httpd_uri_t uri_hotspot_detect = {
    .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handler_hotspot_detect,
};

/* ---------- public API ---------- */

esp_err_t captive_portal_start(void)
{
    /* Start DNS redirect */
    esp_err_t err = dns_server_start(PORTAL_IP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DNS server start failed");
        return err;
    }

    /* Start HTTP server */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.stack_size = 6144;

    err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(err));
        dns_server_stop();
        return err;
    }

    /* Register URI handlers — order matters for wildcard matching */
    httpd_register_uri_handler(s_server, &uri_root);
    httpd_register_uri_handler(s_server, &uri_config_get);
    httpd_register_uri_handler(s_server, &uri_config_post);
    httpd_register_uri_handler(s_server, &uri_wifi_scan);
    httpd_register_uri_handler(s_server, &uri_system_status);
    httpd_register_uri_handler(s_server, &uri_system_reboot);
    httpd_register_uri_handler(s_server, &uri_factory_reset);
    httpd_register_uri_handler(s_server, &uri_generate_204);
    httpd_register_uri_handler(s_server, &uri_hotspot_detect);

    /* OTA handlers */
    ota_manager_register_handlers(s_server);

    /* Catch-all must be last */
    static const httpd_uri_t uri_catch_all = {
        .uri = "/*", .method = HTTP_GET, .handler = handler_catch_all,
    };
    httpd_register_uri_handler(s_server, &uri_catch_all);

    ESP_LOGI(TAG, "Captive portal started at http://%s/", PORTAL_IP);
    return ESP_OK;
}

void captive_portal_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    dns_server_stop();
    ESP_LOGI(TAG, "Captive portal stopped");
}
