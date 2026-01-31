#include "ota_manager.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "cJSON.h"

#include <string.h>

static const char *TAG = "ota_mgr";

/* GET /api/ota/status — firmware info */
static esp_err_t handler_ota_status(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "date", app->date);
    cJSON_AddStringToObject(root, "time", app->time);
    cJSON_AddStringToObject(root, "idf_ver", app->idf_ver);

    if (running) {
        cJSON_AddStringToObject(root, "running_partition", running->label);
        cJSON_AddNumberToObject(root, "running_size", running->size);
    }
    if (update) {
        cJSON_AddStringToObject(root, "update_partition", update->label);
        cJSON_AddNumberToObject(root, "update_size", update->size);
    }

    cJSON_AddNumberToObject(root, "free_heap", (double)esp_get_free_heap_size());

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}

/* POST /api/ota/upload — receive firmware binary */
static esp_err_t handler_ota_upload(httpd_req_t *req)
{
    ESP_LOGI(TAG, "OTA upload started, content_len=%d", req->content_len);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }

    if (req->content_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    if ((size_t)req->content_len > update_part->size) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large for partition");
        return ESP_FAIL;
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_part, req->content_len, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int remaining = req->content_len;
    int received_total = 0;

    while (remaining > 0) {
        int to_read = remaining > 4096 ? 4096 : remaining;
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            ESP_LOGE(TAG, "OTA recv failed at %d/%d", received_total, req->content_len);
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Receive timeout");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        remaining -= received;
        received_total += received;

        if (received_total % (64 * 1024) < 4096) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes", received_total, req->content_len);
        }
    }

    free(buf);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA verify failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful (%d bytes), will reboot", received_total);

    /* Send success response before rebooting */
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "msg", "OTA update successful, rebooting...");
    char *json = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);

    /* Delayed reboot */
    const esp_timer_create_args_t timer_args = {
        .callback = (void (*)(void *))esp_restart,
        .name = "ota_reboot",
    };
    esp_timer_handle_t tmr;
    esp_timer_create(&timer_args, &tmr);
    esp_timer_start_once(tmr, 1000 * 1000); /* 1 second */

    return ESP_OK;
}

/* URI definitions */
static const httpd_uri_t uri_ota_status = {
    .uri = "/api/ota/status",
    .method = HTTP_GET,
    .handler = handler_ota_status,
};

static const httpd_uri_t uri_ota_upload = {
    .uri = "/api/ota/upload",
    .method = HTTP_POST,
    .handler = handler_ota_upload,
};

esp_err_t ota_manager_register_handlers(httpd_handle_t server)
{
    httpd_register_uri_handler(server, &uri_ota_status);
    httpd_register_uri_handler(server, &uri_ota_upload);
    ESP_LOGI(TAG, "OTA handlers registered");
    return ESP_OK;
}
