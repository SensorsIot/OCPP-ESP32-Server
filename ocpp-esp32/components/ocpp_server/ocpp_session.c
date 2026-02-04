#include "ocpp_server.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <time.h>
#include <inttypes.h>

static const char *TAG = "ocpp_sess";

static ocpp_session_t s_session;
static int s_next_transaction_id = 1;

/* Callbacks — support up to 2 subscribers each (MQTT + phase_control) */
#define MAX_CBS 2
static ocpp_status_cb_t  s_status_cbs[MAX_CBS];
static ocpp_session_cb_t s_session_cbs[MAX_CBS];
static ocpp_meter_cb_t   s_meter_cbs[MAX_CBS];

void ocpp_server_set_status_cb(ocpp_status_cb_t cb)
{
    for (int i = 0; i < MAX_CBS; i++) {
        if (!s_status_cbs[i]) { s_status_cbs[i] = cb; return; }
    }
}
void ocpp_server_set_session_cb(ocpp_session_cb_t cb)
{
    for (int i = 0; i < MAX_CBS; i++) {
        if (!s_session_cbs[i]) { s_session_cbs[i] = cb; return; }
    }
}
void ocpp_server_set_meter_cb(ocpp_meter_cb_t cb)
{
    for (int i = 0; i < MAX_CBS; i++) {
        if (!s_meter_cbs[i]) { s_meter_cbs[i] = cb; return; }
    }
}

const ocpp_session_t *ocpp_server_get_session(void)
{
    return &s_session;
}

const char *ocpp_status_str(ocpp_status_t status)
{
    switch (status) {
    case OCPP_STATUS_AVAILABLE:      return "Available";
    case OCPP_STATUS_PREPARING:      return "Preparing";
    case OCPP_STATUS_CHARGING:       return "Charging";
    case OCPP_STATUS_SUSPENDED_EV:   return "SuspendedEV";
    case OCPP_STATUS_SUSPENDED_EVSE: return "SuspendedEVSE";
    case OCPP_STATUS_FINISHING:      return "Finishing";
    case OCPP_STATUS_RESERVED:       return "Reserved";
    case OCPP_STATUS_UNAVAILABLE:    return "Unavailable";
    case OCPP_STATUS_FAULTED:        return "Faulted";
    default:                         return "Unknown";
    }
}

static ocpp_status_t status_from_str(const char *s)
{
    if (!s) return OCPP_STATUS_AVAILABLE;
    if (strcmp(s, "Available") == 0)      return OCPP_STATUS_AVAILABLE;
    if (strcmp(s, "Preparing") == 0)      return OCPP_STATUS_PREPARING;
    if (strcmp(s, "Charging") == 0)       return OCPP_STATUS_CHARGING;
    if (strcmp(s, "SuspendedEV") == 0)    return OCPP_STATUS_SUSPENDED_EV;
    if (strcmp(s, "SuspendedEVSE") == 0)  return OCPP_STATUS_SUSPENDED_EVSE;
    if (strcmp(s, "Finishing") == 0)      return OCPP_STATUS_FINISHING;
    if (strcmp(s, "Reserved") == 0)       return OCPP_STATUS_RESERVED;
    if (strcmp(s, "Unavailable") == 0)    return OCPP_STATUS_UNAVAILABLE;
    if (strcmp(s, "Faulted") == 0)        return OCPP_STATUS_FAULTED;
    return OCPP_STATUS_AVAILABLE;
}

/* Called by ocpp_server.c when BootNotification is received */
void ocpp_session_on_boot(const char *charge_point_id)
{
    strlcpy(s_session.charge_point_id, charge_point_id, sizeof(s_session.charge_point_id));
    s_session.connected = true;
    s_session.status = OCPP_STATUS_AVAILABLE;
    ESP_LOGI(TAG, "Charge point registered: %s", charge_point_id);
}

void ocpp_session_on_disconnect(void)
{
    s_session.connected = false;
    ESP_LOGW(TAG, "Charge point disconnected: %s", s_session.charge_point_id);
}

/* Handle StatusNotification from charger */
void ocpp_session_on_status(int connector_id, const char *status_str,
                             const char *error_code)
{
    ocpp_status_t new_status = status_from_str(status_str);
    s_session.status = new_status;

    ESP_LOGI(TAG, "Connector %d status: %s (error: %s)",
             connector_id, status_str, error_code ? error_code : "NoError");

    for (int i = 0; i < MAX_CBS; i++) {
        if (s_status_cbs[i]) s_status_cbs[i](connector_id, new_status, error_code ? error_code : "NoError");
    }
}

/* Handle StartTransaction from charger */
int ocpp_session_on_start_transaction(int connector_id, const char *id_tag,
                                       int meter_start, const char *timestamp)
{
    s_session.active = true;
    s_session.transaction_id = s_next_transaction_id++;
    s_session.connector_id = connector_id;
    strlcpy(s_session.id_tag, id_tag ? id_tag : "", sizeof(s_session.id_tag));
    s_session.meter_start = meter_start;
    s_session.meter_current = meter_start;
    s_session.start_time = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Transaction started: id=%d connector=%d tag=%s meter=%d",
             s_session.transaction_id, connector_id, s_session.id_tag, meter_start);

    for (int i = 0; i < MAX_CBS; i++) {
        if (s_session_cbs[i]) s_session_cbs[i](&s_session, true);
    }

    return s_session.transaction_id;
}

/* Handle StopTransaction from charger */
void ocpp_session_on_stop_transaction(int transaction_id, int meter_stop,
                                       const char *reason)
{
    s_session.active = false;
    s_session.meter_current = meter_stop;

    ESP_LOGI(TAG, "Transaction stopped: id=%d meter=%d reason=%s",
             transaction_id, meter_stop, reason ? reason : "Local");

    for (int i = 0; i < MAX_CBS; i++) {
        if (s_session_cbs[i]) s_session_cbs[i](&s_session, false);
    }
}

/* Handle MeterValues from charger */
void ocpp_session_on_meter_values(int connector_id, int transaction_id,
                                    const cJSON *meter_values)
{
    /* Update meter readings if available */
    if (meter_values && cJSON_IsArray(meter_values)) {
        cJSON *first = cJSON_GetArrayItem(meter_values, 0);
        if (first) {
            cJSON *sampled = cJSON_GetObjectItem(first, "sampledValue");
            if (sampled && cJSON_IsArray(sampled)) {
                cJSON *sv;
                cJSON_ArrayForEach(sv, sampled) {
                    cJSON *measurand = cJSON_GetObjectItem(sv, "measurand");
                    cJSON *value = cJSON_GetObjectItem(sv, "value");
                    if (measurand && value && cJSON_IsString(value)) {
                        const char *m = measurand->valuestring;
                        if (strcmp(m, "Energy.Active.Import.Register") == 0) {
                            s_session.meter_current = atoi(value->valuestring);
                        } else if (strcmp(m, "Power.Active.Import") == 0) {
                            s_session.power_w = atoi(value->valuestring);
                        } else if (strcmp(m, "Current.Import") == 0) {
                            s_session.current_a = (float)atof(value->valuestring);
                        }
                    }
                }
            }
        }
    }

    ESP_LOGI(TAG, "MeterValues: energy=%" PRId32 "Wh power=%" PRId32 "W current=%.1fA",
             s_session.meter_current, s_session.power_w, s_session.current_a);

    for (int i = 0; i < MAX_CBS; i++) {
        if (s_meter_cbs[i]) s_meter_cbs[i](connector_id, transaction_id, meter_values);
    }
}
