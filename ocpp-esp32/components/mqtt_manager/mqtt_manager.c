#include "mqtt_manager.h"
#include "config_manager.h"
#include "led_status.h"
#include "ocpp_server.h"
#include "ocpp_charging_profile.h"
#include "phase_control.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

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

/* Publish status (simplified FSD 4.6.2) */
static void on_ocpp_status(int connector_id, ocpp_status_t status,
                            const char *error_code)
{
    if (!s_connected) return;

    const ocpp_session_t *sess = ocpp_server_get_session();

    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddBoolToObject(root, "connected", sess->connected);
    cJSON_AddStringToObject(root, "status", ocpp_status_str(status));
    cJSON_AddStringToObject(root, "error_code", error_code);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "MQTT publish status: %s error=%s", ocpp_status_str(status), error_code);
    mqtt_publish("status", json, 1);
    free(json);
}

/* Publish session with meter values (simplified FSD 4.6.2 - consolidated) */
static void on_ocpp_session(const ocpp_session_t *session, bool started)
{
    if (!s_connected) return;

    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddNumberToObject(root, "transaction_id", session->transaction_id);
    cJSON_AddStringToObject(root, "id_tag", session->id_tag);

    int energy_wh = session->meter_current - session->meter_start;
    cJSON_AddNumberToObject(root, "energy_wh", energy_wh);
    cJSON_AddNumberToObject(root, "power_w", session->power_w);
    cJSON_AddNumberToObject(root, "current_a", session->current_a);

    int64_t now_ms = esp_timer_get_time() / 1000;
    int duration_s = (int)((now_ms - session->start_time) / 1000);
    cJSON_AddNumberToObject(root, "duration_s", duration_s);

    /* Get phase mode from phase_control */
    const char *phase_str = (phase_control_get_mode() == PHASE_MODE_1) ? "1-phase" : "3-phase";
    cJSON_AddStringToObject(root, "phase_mode", phase_str);

    cJSON_AddBoolToObject(root, "active", started);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "MQTT publish session: txn=%d energy=%dWh power=%" PRId32 "W",
             session->transaction_id, energy_wh, session->power_w);
    mqtt_publish("session", json, 1);
    free(json);
}

/* Meter values callback - publish consolidated session (FSD 4.6.2) */
static void on_ocpp_meter(int connector_id, int transaction_id,
                           const cJSON *values)
{
    if (!s_connected) return;

    /* Re-use session publisher with updated meter values */
    const ocpp_session_t *session = ocpp_server_get_session();
    if (session->active) {
        on_ocpp_session(session, true);
    }
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
        /* Simplified: power_w triggers automatic phase switching (FSD 4.6.2) */
        cJSON *power_w = cJSON_GetObjectItem(root, "power_w");

        if (power_w && cJSON_IsNumber(power_w)) {
            int watts = (int)power_w->valuedouble;
            ESP_LOGI(TAG, "Power limit command: %dW", watts);

            /* Automatic phase switching based on power threshold (FSD 4.10.8) */
            /* < 4100W = 1-phase, >= 4100W = 3-phase */
            phase_mode_t current = phase_control_get_mode();
            phase_mode_t target = (watts < 4100) ? PHASE_MODE_1 : PHASE_MODE_3;

            if (target != current) {
                ESP_LOGI(TAG, "Auto phase switch: %s -> %s (power=%dW)",
                         current == PHASE_MODE_1 ? "1-phase" : "3-phase",
                         target == PHASE_MODE_1 ? "1-phase" : "3-phase",
                         watts);
                phase_control_request_switch(target);
            }

            /* Convert watts to amps based on phase mode */
            float amps;
            if (target == PHASE_MODE_1) {
                /* 1-phase: P = V * I, I = P / V */
                amps = (float)watts / 230.0f;
            } else {
                /* 3-phase: P = 3 * V * I, I = P / (3 * V) */
                amps = (float)watts / (3.0f * 230.0f);
            }

            /* Cap at 16A max */
            if (amps > 16.0f) amps = 16.0f;

            cJSON *profile = ocpp_profile_build_set_payload(1, amps, "TxDefaultProfile");
            cJSON *cs = cJSON_GetObjectItem(profile, "csChargingProfiles");
            if (cs) {
                ocpp_send_set_charging_profile(1, cs);
            }
            cJSON_Delete(profile);
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

    /* Enable verbose MQTT library logging for debugging */
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_DEBUG);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_DEBUG);
    esp_log_level_set("TRANSPORT", ESP_LOG_DEBUG);
    esp_log_level_set("OUTBOX", ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "MQTT config: host='%s' port=%d prefix='%s' client_id='%s'",
             cfg->mqtt_host, cfg->mqtt_port, cfg->mqtt_prefix, cfg->mqtt_client_id);

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

esp_err_t mqtt_publish_phase_status(const char *phase_mode, float correction_factor)
{
    cJSON *root = cJSON_CreateObject();
    char ts[30];
    get_timestamp(ts, sizeof(ts));
    cJSON_AddStringToObject(root, "timestamp", ts);
    cJSON_AddStringToObject(root, "phase_mode", phase_mode);
    cJSON_AddNumberToObject(root, "power_correction_factor", correction_factor);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "MQTT publish phase: %s factor=%.1f", phase_mode, correction_factor);
    esp_err_t ret = mqtt_publish("phase", json, 1);
    free(json);
    return ret;
}
