#include "ocpp_server.h"
#include "ocpp_msg.h"
#include "ocpp_charging_profile.h"
#include "config_manager.h"
#include "led_status.h"
#include "gpio_control.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include <string.h>
#include <time.h>

static const char *TAG = "ocpp_srv";

static httpd_handle_t s_server;

/* Active WebSocket file descriptor (single client support) */
static int s_ws_fd = -1;

/* Forward declarations for session functions defined in ocpp_session.c */
extern void ocpp_session_on_boot(const char *charge_point_id);
extern void ocpp_session_on_disconnect(void);
extern void ocpp_session_on_status(int connector_id, const char *status_str,
                                    const char *error_code);
extern int  ocpp_session_on_start_transaction(int connector_id, const char *id_tag,
                                               int meter_start, const char *timestamp);
extern void ocpp_session_on_stop_transaction(int transaction_id, int meter_stop,
                                              const char *reason);
extern void ocpp_session_on_meter_values(int connector_id, int transaction_id,
                                          const cJSON *meter_values);

/* Pending request tracking (for Call messages we send) */
#define MAX_PENDING_REQUESTS 4
typedef struct {
    char unique_id[40];
    char action[40];
    bool active;
    int64_t timestamp;
} pending_req_t;

static pending_req_t s_pending[MAX_PENDING_REQUESTS];

static void track_pending(const char *unique_id, const char *action)
{
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (!s_pending[i].active) {
            strlcpy(s_pending[i].unique_id, unique_id, sizeof(s_pending[i].unique_id));
            strlcpy(s_pending[i].action, action, sizeof(s_pending[i].action));
            s_pending[i].active = true;
            s_pending[i].timestamp = esp_timer_get_time();
            return;
        }
    }
    ESP_LOGW(TAG, "No free pending request slot");
}

static const char *find_pending_action(const char *unique_id)
{
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (s_pending[i].active && strcmp(s_pending[i].unique_id, unique_id) == 0) {
            s_pending[i].active = false;
            return s_pending[i].action;
        }
    }
    return NULL;
}

/* ---------- Send helper ---------- */

static esp_err_t ws_send(const char *data)
{
    if (s_ws_fd < 0 || !s_server) return ESP_ERR_INVALID_STATE;

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)data,
        .len = strlen(data),
    };

    esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fd, &frame);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS send failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t send_call(const char *action, cJSON *payload)
{
    char uid[40];
    char *msg = ocpp_msg_call(action, payload, uid, sizeof(uid));
    if (!msg) return ESP_ERR_NO_MEM;

    track_pending(uid, action);
    ESP_LOGI(TAG, "TX Call: %s [%s]", action, uid);
    esp_err_t ret = ws_send(msg);
    free(msg);
    return ret;
}

static esp_err_t send_result(const char *unique_id, cJSON *payload)
{
    char *msg = ocpp_msg_call_result(unique_id, payload);
    if (!msg) return ESP_ERR_NO_MEM;

    esp_err_t ret = ws_send(msg);
    free(msg);
    return ret;
}

static esp_err_t send_error(const char *unique_id, const char *code, const char *desc)
{
    char *msg = ocpp_msg_call_error(unique_id, code, desc);
    if (!msg) return ESP_ERR_NO_MEM;

    esp_err_t ret = ws_send(msg);
    free(msg);
    return ret;
}

/* ---------- OCPP Call handlers (charger → server) ---------- */

static void handle_boot_notification(const char *uid, const cJSON *payload)
{
    cJSON *model = cJSON_GetObjectItem(payload, "chargePointModel");
    cJSON *vendor = cJSON_GetObjectItem(payload, "chargePointVendor");
    cJSON *serial = cJSON_GetObjectItem(payload, "chargePointSerialNumber");

    ESP_LOGI(TAG, "BootNotification: vendor=%s model=%s serial=%s",
             vendor ? vendor->valuestring : "?",
             model ? model->valuestring : "?",
             serial ? serial->valuestring : "?");

    const config_t *cfg = config_get();

    /* Use serial number or model as charge point ID */
    const char *cp_id = serial ? serial->valuestring :
                        model ? model->valuestring : "unknown";
    ocpp_session_on_boot(cp_id);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "Accepted");
    cJSON_AddNumberToObject(resp, "interval", cfg->hb_interval);

    /* ISO 8601 timestamp */
    time_t now;
    time(&now);
    struct tm tm;
    gmtime_r(&now, &tm);
    char ts[30];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
    cJSON_AddStringToObject(resp, "currentTime", ts);

    send_result(uid, resp);
    led_status_set(LED_ID_OCPP, LED_PATTERN_SOLID);
}

static void handle_heartbeat(const char *uid, const cJSON *payload)
{
    cJSON *resp = cJSON_CreateObject();

    time_t now;
    time(&now);
    struct tm tm;
    gmtime_r(&now, &tm);
    char ts[30];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm);
    cJSON_AddStringToObject(resp, "currentTime", ts);

    send_result(uid, resp);
}

static void handle_status_notification(const char *uid, const cJSON *payload)
{
    cJSON *conn_id = cJSON_GetObjectItem(payload, "connectorId");
    cJSON *status = cJSON_GetObjectItem(payload, "status");
    cJSON *error = cJSON_GetObjectItem(payload, "errorCode");

    int cid = (conn_id && cJSON_IsNumber(conn_id)) ? (int)conn_id->valuedouble : 0;
    const char *st = (status && cJSON_IsString(status)) ? status->valuestring : "Unknown";
    const char *ec = (error && cJSON_IsString(error)) ? error->valuestring : "NoError";

    ocpp_session_on_status(cid, st, ec);
    send_result(uid, cJSON_CreateObject());
}

static void handle_authorize(const char *uid, const cJSON *payload)
{
    cJSON *id_tag = cJSON_GetObjectItem(payload, "idTag");
    ESP_LOGI(TAG, "Authorize: tag=%s",
             (id_tag && cJSON_IsString(id_tag)) ? id_tag->valuestring : "?");

    /* Accept all mode */
    cJSON *resp = cJSON_CreateObject();
    cJSON *id_info = cJSON_CreateObject();
    cJSON_AddStringToObject(id_info, "status", "Accepted");
    cJSON_AddItemToObject(resp, "idTagInfo", id_info);
    send_result(uid, resp);
}

static void handle_start_transaction(const char *uid, const cJSON *payload)
{
    cJSON *conn_id = cJSON_GetObjectItem(payload, "connectorId");
    cJSON *id_tag = cJSON_GetObjectItem(payload, "idTag");
    cJSON *meter = cJSON_GetObjectItem(payload, "meterStart");
    cJSON *ts = cJSON_GetObjectItem(payload, "timestamp");

    int cid = (conn_id && cJSON_IsNumber(conn_id)) ? (int)conn_id->valuedouble : 1;
    const char *tag = (id_tag && cJSON_IsString(id_tag)) ? id_tag->valuestring : "";
    int meter_start = (meter && cJSON_IsNumber(meter)) ? (int)meter->valuedouble : 0;
    const char *timestamp = (ts && cJSON_IsString(ts)) ? ts->valuestring : "";

    int txn_id = ocpp_session_on_start_transaction(cid, tag, meter_start, timestamp);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "transactionId", txn_id);
    cJSON *id_info = cJSON_CreateObject();
    cJSON_AddStringToObject(id_info, "status", "Accepted");
    cJSON_AddItemToObject(resp, "idTagInfo", id_info);
    send_result(uid, resp);
}

static void handle_stop_transaction(const char *uid, const cJSON *payload)
{
    cJSON *txn_id = cJSON_GetObjectItem(payload, "transactionId");
    cJSON *meter = cJSON_GetObjectItem(payload, "meterStop");
    cJSON *reason = cJSON_GetObjectItem(payload, "reason");

    int tid = (txn_id && cJSON_IsNumber(txn_id)) ? (int)txn_id->valuedouble : 0;
    int meter_stop = (meter && cJSON_IsNumber(meter)) ? (int)meter->valuedouble : 0;
    const char *r = (reason && cJSON_IsString(reason)) ? reason->valuestring : "Local";

    ocpp_session_on_stop_transaction(tid, meter_stop, r);

    cJSON *resp = cJSON_CreateObject();
    cJSON *id_info = cJSON_CreateObject();
    cJSON_AddStringToObject(id_info, "status", "Accepted");
    cJSON_AddItemToObject(resp, "idTagInfo", id_info);
    send_result(uid, resp);
}

static void handle_meter_values(const char *uid, const cJSON *payload)
{
    cJSON *conn_id = cJSON_GetObjectItem(payload, "connectorId");
    cJSON *txn_id = cJSON_GetObjectItem(payload, "transactionId");
    cJSON *values = cJSON_GetObjectItem(payload, "meterValue");

    int cid = (conn_id && cJSON_IsNumber(conn_id)) ? (int)conn_id->valuedouble : 0;
    int tid = (txn_id && cJSON_IsNumber(txn_id)) ? (int)txn_id->valuedouble : 0;

    ocpp_session_on_meter_values(cid, tid, values);
    send_result(uid, cJSON_CreateObject());

    /* Blink OCPP LED on data */
    led_status_set(LED_ID_OCPP, LED_PATTERN_FAST_BLINK);
    /* Restore after brief blink — timer could do this properly */
}

static void handle_data_transfer(const char *uid, const cJSON *payload)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "Accepted");
    send_result(uid, resp);
}

static void handle_diagnostics_status(const char *uid, const cJSON *payload)
{
    send_result(uid, cJSON_CreateObject());
}

static void handle_firmware_status(const char *uid, const cJSON *payload)
{
    send_result(uid, cJSON_CreateObject());
}

/* ---------- OCPP CallResult handlers (responses to our Calls) ---------- */

static void handle_call_result(const char *action, const cJSON *payload)
{
    ESP_LOGI(TAG, "CallResult for %s", action);

    if (strcmp(action, "RemoteStartTransaction") == 0 ||
        strcmp(action, "RemoteStopTransaction") == 0) {
        cJSON *status = cJSON_GetObjectItem(payload, "status");
        ESP_LOGI(TAG, "%s response: %s", action,
                 (status && cJSON_IsString(status)) ? status->valuestring : "?");
    } else if (strcmp(action, "SetChargingProfile") == 0) {
        cJSON *status = cJSON_GetObjectItem(payload, "status");
        ESP_LOGI(TAG, "SetChargingProfile response: %s",
                 (status && cJSON_IsString(status)) ? status->valuestring : "?");
    } else if (strcmp(action, "ClearChargingProfile") == 0) {
        cJSON *status = cJSON_GetObjectItem(payload, "status");
        ESP_LOGI(TAG, "ClearChargingProfile response: %s",
                 (status && cJSON_IsString(status)) ? status->valuestring : "?");
    }
}

/* ---------- Message router ---------- */

static void route_message(const char *data, int len)
{
    ocpp_parsed_msg_t msg;
    if (ocpp_msg_parse(data, len, &msg) != 0) {
        ESP_LOGW(TAG, "Failed to parse OCPP message");
        return;
    }

    switch (msg.type) {
    case OCPP_MSG_CALL: {
        ESP_LOGI(TAG, "RX Call: %s [%s]", msg.action, msg.unique_id);

        if (strcmp(msg.action, "BootNotification") == 0)
            handle_boot_notification(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "Heartbeat") == 0)
            handle_heartbeat(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "StatusNotification") == 0)
            handle_status_notification(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "Authorize") == 0)
            handle_authorize(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "StartTransaction") == 0)
            handle_start_transaction(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "StopTransaction") == 0)
            handle_stop_transaction(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "MeterValues") == 0)
            handle_meter_values(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "DataTransfer") == 0)
            handle_data_transfer(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "DiagnosticsStatusNotification") == 0)
            handle_diagnostics_status(msg.unique_id, msg.payload);
        else if (strcmp(msg.action, "FirmwareStatusNotification") == 0)
            handle_firmware_status(msg.unique_id, msg.payload);
        else {
            ESP_LOGW(TAG, "Unknown action: %s", msg.action);
            send_error(msg.unique_id, "NotImplemented",
                      "Action not supported");
        }
        break;
    }
    case OCPP_MSG_CALLRESULT: {
        const char *action = find_pending_action(msg.unique_id);
        if (action) {
            handle_call_result(action, msg.payload);
        } else {
            ESP_LOGW(TAG, "Unexpected CallResult: %s", msg.unique_id);
        }
        break;
    }
    case OCPP_MSG_CALLERROR: {
        const char *action = find_pending_action(msg.unique_id);
        ESP_LOGW(TAG, "CallError for %s: %s", action ? action : "?", msg.action);
        break;
    }
    }

    ocpp_msg_free(&msg);
}

/* ---------- WebSocket handler ---------- */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* WebSocket handshake */
        ESP_LOGI(TAG, "WS handshake from fd=%d uri=%s", httpd_req_to_sockfd(req), req->uri);
        s_ws_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    /* Receive WebSocket frame */
    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    /* Get frame length */
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS recv frame len failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (frame.len == 0) {
        return ESP_OK;
    }

    if (frame.len > 4096) {
        ESP_LOGW(TAG, "WS frame too large: %d", frame.len);
        return ESP_FAIL;
    }

    frame.payload = malloc(frame.len + 1);
    if (!frame.payload) return ESP_ERR_NO_MEM;

    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) {
        free(frame.payload);
        return ret;
    }
    frame.payload[frame.len] = '\0';

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        route_message((const char *)frame.payload, frame.len);
    } else if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "WS client closed connection");
        s_ws_fd = -1;
        ocpp_session_on_disconnect();
        led_status_set(LED_ID_OCPP, LED_PATTERN_OFF);
    } else if (frame.type == HTTPD_WS_TYPE_PING) {
        /* Send pong */
        httpd_ws_frame_t pong = {
            .type = HTTPD_WS_TYPE_PONG,
            .payload = frame.payload,
            .len = frame.len,
        };
        httpd_ws_send_frame(req, &pong);
    }

    free(frame.payload);
    return ESP_OK;
}

/* ---------- Server → Charger commands ---------- */

esp_err_t ocpp_send_remote_start(int connector_id, const char *id_tag)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "connectorId", connector_id);
    cJSON_AddStringToObject(p, "idTag", id_tag ? id_tag : "ENERGY_MANAGER");
    return send_call("RemoteStartTransaction", p);
}

esp_err_t ocpp_send_remote_stop(int transaction_id)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "transactionId", transaction_id);
    return send_call("RemoteStopTransaction", p);
}

esp_err_t ocpp_send_change_availability(int connector_id, const char *type)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "connectorId", connector_id);
    cJSON_AddStringToObject(p, "type", type ? type : "Operative");
    return send_call("ChangeAvailability", p);
}

esp_err_t ocpp_send_reset(const char *type)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "type", type ? type : "Soft");
    return send_call("Reset", p);
}

esp_err_t ocpp_send_unlock_connector(int connector_id)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "connectorId", connector_id);
    return send_call("UnlockConnector", p);
}

esp_err_t ocpp_send_trigger_message(const char *requested_message)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "requestedMessage", requested_message);
    return send_call("TriggerMessage", p);
}

esp_err_t ocpp_send_set_charging_profile(int connector_id, const cJSON *profile)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddNumberToObject(p, "connectorId", connector_id);
    cJSON_AddItemToObject(p, "csChargingProfiles", cJSON_Duplicate(profile, true));

    /* Store locally too */
    ocpp_profile_set(profile, connector_id);

    return send_call("SetChargingProfile", p);
}

esp_err_t ocpp_send_clear_charging_profile(int id, int connector_id,
                                            const char *purpose, int stack_level)
{
    cJSON *p = cJSON_CreateObject();
    if (id >= 0) cJSON_AddNumberToObject(p, "id", id);
    if (connector_id >= 0) cJSON_AddNumberToObject(p, "connectorId", connector_id);
    if (purpose) cJSON_AddStringToObject(p, "chargingProfilePurpose", purpose);
    if (stack_level >= 0) cJSON_AddNumberToObject(p, "stackLevel", stack_level);

    ocpp_profile_clear(id, connector_id, purpose, stack_level);

    return send_call("ClearChargingProfile", p);
}

esp_err_t ocpp_send_get_configuration(const char *key)
{
    cJSON *p = cJSON_CreateObject();
    if (key) {
        cJSON *keys = cJSON_CreateArray();
        cJSON_AddItemToArray(keys, cJSON_CreateString(key));
        cJSON_AddItemToObject(p, "key", keys);
    }
    return send_call("GetConfiguration", p);
}

esp_err_t ocpp_send_change_configuration(const char *key, const char *value)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "key", key);
    cJSON_AddStringToObject(p, "value", value);
    return send_call("ChangeConfiguration", p);
}

esp_err_t ocpp_send_update_firmware(const char *location, const char *retrieve_date,
                                     int retries, int retry_interval)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "location", location);
    cJSON_AddStringToObject(p, "retrieveDate", retrieve_date);
    if (retries > 0) cJSON_AddNumberToObject(p, "retries", retries);
    if (retry_interval > 0) cJSON_AddNumberToObject(p, "retryInterval", retry_interval);
    return send_call("UpdateFirmware", p);
}

esp_err_t ocpp_send_get_diagnostics(const char *location, const char *start_time,
                                     const char *stop_time, int retries, int retry_interval)
{
    cJSON *p = cJSON_CreateObject();
    cJSON_AddStringToObject(p, "location", location);
    if (start_time) cJSON_AddStringToObject(p, "startTime", start_time);
    if (stop_time) cJSON_AddStringToObject(p, "stopTime", stop_time);
    if (retries > 0) cJSON_AddNumberToObject(p, "retries", retries);
    if (retry_interval > 0) cJSON_AddNumberToObject(p, "retryInterval", retry_interval);
    return send_call("GetDiagnostics", p);
}

/* ---------- Public API ---------- */

esp_err_t ocpp_server_start(void)
{
    const config_t *cfg = config_get();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = cfg->ws_port;
    config.max_uri_handlers = 4;
    config.stack_size = 8192;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OCPP server on port %d: %s",
                 cfg->ws_port, esp_err_to_name(err));
        return err;
    }

    /* Register WebSocket handler */
    static const httpd_uri_t ws_uri = {
        .uri = "/ocpp/*",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .handle_ws_control_frames = true,
        .supported_subprotocol = "ocpp1.6",
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    ESP_LOGI(TAG, "OCPP WebSocket server started on port %d", cfg->ws_port);
    return ESP_OK;
}

void ocpp_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    s_ws_fd = -1;
    ESP_LOGI(TAG, "OCPP server stopped");
}
