#include "mqtt_manager.h"
#include "config_manager.h"
#include "led_status.h"
#include "ocpp_server.h"
#include "ocpp_charging_profile.h"
#include "phase_control.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "mqtt_mgr";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;

/* Topic buffer */
static char s_prefix[140]; /* e.g. "ocpp/charger1" */

static void build_topic(char *buf, size_t sz, const char *sub)
{
    snprintf(buf, sz, "%s/%s", s_prefix, sub);
}

static void get_timestamp(char *buf, size_t sz)
{
    time_t now;
    time(&now);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(buf, sz, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* ---------- OCPP callbacks → MQTT publish ---------- */

static void on_ocpp_status(int connector_id, ocpp_status_t status,
                            const char *error_code)
{
    if (!s_connected) return;

    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddNumberToObject(root, "connector_id", connector_id);
    cJSON_AddStringToObject(root, "status", ocpp_status_str(status));
    cJSON_AddStringToObject(root, "error_code", error_code);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    mqtt_publish("status", json, 1);

    /* Publish to dedicated availability topic */
    const char *st = ocpp_status_str(status);
    if (strcmp(st, "Available") == 0 || strcmp(st, "Unavailable") == 0) {
        cJSON *avail = cJSON_CreateObject();
        cJSON_AddStringToObject(avail, "timestamp", ts);
        cJSON_AddNumberToObject(avail, "connector_id", connector_id);
        cJSON_AddStringToObject(avail, "availability", st);
        char *avail_json = cJSON_PrintUnformatted(avail);
        cJSON_Delete(avail);
        mqtt_publish("availability", avail_json, 1);
        free(avail_json);
    }

    /* Publish to error topic on faults */
    if (strcmp(error_code, "NoError") != 0) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "timestamp", ts);
        cJSON_AddNumberToObject(err, "connector_id", connector_id);
        cJSON_AddStringToObject(err, "error_code", error_code);
        cJSON_AddStringToObject(err, "status", st);
        char *err_json = cJSON_PrintUnformatted(err);
        cJSON_Delete(err);
        mqtt_publish("error", err_json, 1);
        free(err_json);
    }

    free(json);
}

static void on_ocpp_session(const ocpp_session_t *session, bool started)
{
    if (!s_connected) return;

    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddNumberToObject(root, "transaction_id", session->transaction_id);
    cJSON_AddNumberToObject(root, "connector_id", session->connector_id);
    cJSON_AddStringToObject(root, "id_tag", session->id_tag);
    cJSON_AddNumberToObject(root, "meter_start", session->meter_start);
    cJSON_AddNumberToObject(root, "meter_current", session->meter_current);
    cJSON_AddBoolToObject(root, "active", started);

    if (!started) {
        int energy = session->meter_current - session->meter_start;
        cJSON_AddNumberToObject(root, "energy_wh", energy);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    mqtt_publish("session", json, 1);
    free(json);
}

static void on_ocpp_meter(int connector_id, int transaction_id,
                           const cJSON *values)
{
    if (!s_connected || !values) return;

    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddNumberToObject(root, "connector_id", connector_id);
    cJSON_AddNumberToObject(root, "transaction_id", transaction_id);
    cJSON_AddItemToObject(root, "values", cJSON_Duplicate(values, true));

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    mqtt_publish("meter", json, 0);
    free(json);
}

/* ---------- MQTT command handling ---------- */

static void handle_command(const char *topic, const char *data, int data_len)
{
    /* Extract sub-topic after prefix */
    const char *sub = topic + strlen(s_prefix) + 1; /* skip prefix/ */
    if (strncmp(sub, "command/", 8) != 0) return;
    sub += 8; /* skip "command/" */

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (!root) {
        ESP_LOGW(TAG, "Invalid command JSON on %s", sub);
        return;
    }

    ESP_LOGI(TAG, "MQTT command: %s", sub);

    if (strcmp(sub, "start") == 0) {
        cJSON *conn = cJSON_GetObjectItem(root, "connector_id");
        cJSON *tag = cJSON_GetObjectItem(root, "id_tag");
        int cid = (conn && cJSON_IsNumber(conn)) ? (int)conn->valuedouble : 1;
        const char *t = (tag && cJSON_IsString(tag)) ? tag->valuestring : "ENERGY_MANAGER";
        ocpp_send_remote_start(cid, t);

    } else if (strcmp(sub, "stop") == 0) {
        const ocpp_session_t *sess = ocpp_server_get_session();
        if (sess->active) {
            ocpp_send_remote_stop(sess->transaction_id);
        }

    } else if (strcmp(sub, "limit") == 0) {
        cJSON *conn = cJSON_GetObjectItem(root, "connector_id");
        cJSON *limit = cJSON_GetObjectItem(root, "current_limit_a");
        cJSON *power = cJSON_GetObjectItem(root, "power_limit_kw");

        float amps = 0;
        if (limit && cJSON_IsNumber(limit)) {
            amps = (float)limit->valuedouble;
        } else if (power && cJSON_IsNumber(power)) {
            /* Convert kW to amps (single phase 230V) */
            amps = (float)(power->valuedouble * 1000.0 / 230.0);
        }

        if (amps > 0) {
            int cid = (conn && cJSON_IsNumber(conn)) ? (int)conn->valuedouble : 1;
            cJSON *profile = ocpp_profile_build_set_payload(cid, amps, "TxDefaultProfile");
            cJSON *cs = cJSON_GetObjectItem(profile, "csChargingProfiles");
            if (cs) {
                ocpp_send_set_charging_profile(cid, cs);
            }
            cJSON_Delete(profile);
        }

    } else if (strcmp(sub, "availability") == 0) {
        cJSON *conn = cJSON_GetObjectItem(root, "connector_id");
        cJSON *type = cJSON_GetObjectItem(root, "type");
        int cid = (conn && cJSON_IsNumber(conn)) ? (int)conn->valuedouble : 0;
        const char *t = (type && cJSON_IsString(type)) ? type->valuestring : "Operative";
        ocpp_send_change_availability(cid, t);

    } else if (strcmp(sub, "reset") == 0) {
        cJSON *type = cJSON_GetObjectItem(root, "type");
        const char *t = (type && cJSON_IsString(type)) ? type->valuestring : "Soft";
        ocpp_send_reset(t);

    } else if (strcmp(sub, "config/get") == 0) {
        cJSON *key = cJSON_GetObjectItem(root, "key");
        const char *k = (key && cJSON_IsString(key)) ? key->valuestring : NULL;
        ocpp_send_get_configuration(k);

    } else if (strcmp(sub, "config/set") == 0) {
        cJSON *key = cJSON_GetObjectItem(root, "key");
        cJSON *val = cJSON_GetObjectItem(root, "value");
        if (key && val && cJSON_IsString(key) && cJSON_IsString(val)) {
            ocpp_send_change_configuration(key->valuestring, val->valuestring);
        }

    } else if (strcmp(sub, "phase") == 0) {
        cJSON *mode = cJSON_GetObjectItem(root, "mode");
        if (mode && cJSON_IsString(mode)) {
            phase_mode_t target;
            if (strcmp(mode->valuestring, "1-phase") == 0 ||
                strcmp(mode->valuestring, "1") == 0) {
                target = PHASE_MODE_1;
            } else {
                target = PHASE_MODE_3;
            }
            phase_control_request_switch(target);
        }

    } else {
        ESP_LOGW(TAG, "Unknown command: %s", sub);
    }

    cJSON_Delete(root);
}

/* ---------- MQTT event handler ---------- */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)arg;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        s_connected = true;
        led_status_set(LED_ID_MQTT, LED_PATTERN_SOLID);

        /* Subscribe to command topics */
        {
            char topic[200];
            build_topic(topic, sizeof(topic), "command/#");
            esp_mqtt_client_subscribe(s_client, topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s", topic);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        s_connected = false;
        led_status_set(LED_ID_MQTT, LED_PATTERN_SLOW_BLINK);
        break;

    case MQTT_EVENT_DATA:
        if (event->topic && event->topic_len > 0 && event->data_len > 0) {
            /* Null-terminate topic for string ops */
            char *topic_buf = malloc(event->topic_len + 1);
            if (topic_buf) {
                memcpy(topic_buf, event->topic, event->topic_len);
                topic_buf[event->topic_len] = '\0';
                handle_command(topic_buf, event->data, event->data_len);
                free(topic_buf);
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* ---------- Public API ---------- */

esp_err_t mqtt_manager_start(void)
{
    const config_t *cfg = config_get();

    if (cfg->mqtt_host[0] == '\0') {
        ESP_LOGW(TAG, "No MQTT host configured, skipping");
        return ESP_OK;
    }

    /* Build prefix */
    const ocpp_session_t *sess = ocpp_server_get_session();
    const char *cp_id = (sess->charge_point_id[0]) ? sess->charge_point_id : cfg->dev_name;
    snprintf(s_prefix, sizeof(s_prefix), "%s/%s", cfg->mqtt_prefix, cp_id);

    /* Build broker URI */
    char uri[128];
    snprintf(uri, sizeof(uri), "%s://%s:%d",
             cfg->mqtt_tls ? "mqtts" : "mqtt",
             cfg->mqtt_host, cfg->mqtt_port);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = cfg->mqtt_client_id,
        .credentials.username = cfg->mqtt_user,
        .credentials.authentication.password = cfg->mqtt_pass,
        .network.reconnect_timeout_ms = 5000,
        .buffer.size = 2048,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "MQTT client init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                    mqtt_event_handler, NULL);

    /* Register OCPP callbacks */
    ocpp_server_set_status_cb(on_ocpp_status);
    ocpp_server_set_session_cb(on_ocpp_session);
    ocpp_server_set_meter_cb(on_ocpp_meter);

    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT client start failed: %s", esp_err_to_name(err));
        return err;
    }

    led_status_set(LED_ID_MQTT, LED_PATTERN_SLOW_BLINK);
    ESP_LOGI(TAG, "MQTT client started: %s prefix=%s", uri, s_prefix);
    return ESP_OK;
}

void mqtt_manager_stop(void)
{
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    led_status_set(LED_ID_MQTT, LED_PATTERN_OFF);
    ESP_LOGI(TAG, "MQTT client stopped");
}

bool mqtt_is_connected(void)
{
    return s_connected;
}

esp_err_t mqtt_publish(const char *topic, const char *json_str, int qos)
{
    if (!s_connected || !s_client) return ESP_ERR_INVALID_STATE;

    char full_topic[200];
    build_topic(full_topic, sizeof(full_topic), topic);

    int msg_id = esp_mqtt_client_publish(s_client, full_topic, json_str,
                                          strlen(json_str), qos, 0);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "MQTT publish failed: %s", topic);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t mqtt_publish_phase_result(bool success, const char *old_mode,
                                     const char *new_mode, int duration_ms,
                                     const char *error)
{
    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddBoolToObject(root, "success", success);
    cJSON_AddStringToObject(root, "old_mode", old_mode);
    cJSON_AddStringToObject(root, "new_mode", new_mode);
    cJSON_AddNumberToObject(root, "switch_duration_ms", duration_ms);
    if (error) cJSON_AddStringToObject(root, "error", error);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    esp_err_t ret = mqtt_publish("phase/result", json, 1);
    free(json);
    return ret;
}
