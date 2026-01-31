#include "ocpp_charging_profile.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <math.h>

static const char *TAG = "ocpp_prof";

static ocpp_charging_profile_t s_profiles[MAX_CHARGING_PROFILES];

void ocpp_profile_set(const cJSON *profile_json, int connector_id)
{
    if (!profile_json) return;

    cJSON *id_item = cJSON_GetObjectItem(profile_json, "chargingProfileId");
    if (!id_item || !cJSON_IsNumber(id_item)) return;

    int profile_id = (int)id_item->valuedouble;

    /* Find existing or empty slot */
    int slot = -1;
    for (int i = 0; i < MAX_CHARGING_PROFILES; i++) {
        if (s_profiles[i].valid && s_profiles[i].profile_id == profile_id) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        for (int i = 0; i < MAX_CHARGING_PROFILES; i++) {
            if (!s_profiles[i].valid) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        ESP_LOGW(TAG, "No free profile slot for id=%d", profile_id);
        return;
    }

    ocpp_charging_profile_t *p = &s_profiles[slot];
    memset(p, 0, sizeof(*p));
    p->valid = true;
    p->profile_id = profile_id;
    p->connector_id = connector_id;
    p->transaction_id = -1;

    cJSON *stack = cJSON_GetObjectItem(profile_json, "stackLevel");
    if (stack && cJSON_IsNumber(stack)) p->stack_level = (int)stack->valuedouble;

    cJSON *purpose = cJSON_GetObjectItem(profile_json, "chargingProfilePurpose");
    if (purpose && cJSON_IsString(purpose))
        strlcpy(p->purpose, purpose->valuestring, sizeof(p->purpose));

    cJSON *kind = cJSON_GetObjectItem(profile_json, "chargingProfileKind");
    if (kind && cJSON_IsString(kind))
        strlcpy(p->kind, kind->valuestring, sizeof(p->kind));

    cJSON *txn = cJSON_GetObjectItem(profile_json, "transactionId");
    if (txn && cJSON_IsNumber(txn)) p->transaction_id = (int)txn->valuedouble;

    cJSON *schedule = cJSON_GetObjectItem(profile_json, "chargingSchedule");
    if (schedule) {
        cJSON *unit = cJSON_GetObjectItem(schedule, "chargingRateUnit");
        if (unit && cJSON_IsString(unit))
            strlcpy(p->rate_unit, unit->valuestring, sizeof(p->rate_unit));

        cJSON *periods = cJSON_GetObjectItem(schedule, "chargingSchedulePeriod");
        if (periods && cJSON_IsArray(periods)) {
            p->num_periods = 0;
            cJSON *period;
            cJSON_ArrayForEach(period, periods) {
                if (p->num_periods >= MAX_SCHEDULE_PERIODS) break;

                ocpp_schedule_period_t *sp = &p->periods[p->num_periods];
                cJSON *start = cJSON_GetObjectItem(period, "startPeriod");
                cJSON *limit = cJSON_GetObjectItem(period, "limit");
                cJSON *phases = cJSON_GetObjectItem(period, "numberPhases");

                sp->start_period = (start && cJSON_IsNumber(start)) ? (int)start->valuedouble : 0;
                sp->limit = (limit && cJSON_IsNumber(limit)) ? (float)limit->valuedouble : 0;
                sp->number_phases = (phases && cJSON_IsNumber(phases)) ? (int)phases->valuedouble : 3;

                p->num_periods++;
            }
        }
    }

    ESP_LOGI(TAG, "Profile set: id=%d purpose=%s stack=%d periods=%d",
             p->profile_id, p->purpose, p->stack_level, p->num_periods);
}

int ocpp_profile_clear(int profile_id, int connector_id,
                        const char *purpose, int stack_level)
{
    int cleared = 0;
    for (int i = 0; i < MAX_CHARGING_PROFILES; i++) {
        if (!s_profiles[i].valid) continue;

        bool match = true;
        if (profile_id >= 0 && s_profiles[i].profile_id != profile_id) match = false;
        if (connector_id >= 0 && s_profiles[i].connector_id != connector_id) match = false;
        if (purpose && purpose[0] && strcmp(s_profiles[i].purpose, purpose) != 0) match = false;
        if (stack_level >= 0 && s_profiles[i].stack_level != stack_level) match = false;

        if (match) {
            s_profiles[i].valid = false;
            cleared++;
        }
    }
    ESP_LOGI(TAG, "Cleared %d profiles", cleared);
    return cleared;
}

float ocpp_profile_get_effective_limit(int connector_id, int transaction_id)
{
    float min_limit = -1.0f;

    for (int i = 0; i < MAX_CHARGING_PROFILES; i++) {
        if (!s_profiles[i].valid) continue;
        if (s_profiles[i].connector_id != 0 &&
            s_profiles[i].connector_id != connector_id) continue;

        /* TxProfile must match transaction */
        if (strcmp(s_profiles[i].purpose, "TxProfile") == 0 &&
            s_profiles[i].transaction_id != transaction_id) continue;

        /* Find applicable period (last one where startPeriod has elapsed) */
        if (s_profiles[i].num_periods > 0) {
            /* For simplicity, use the first period's limit */
            float limit = s_profiles[i].periods[0].limit;
            if (min_limit < 0 || limit < min_limit) {
                min_limit = limit;
            }
        }
    }

    return min_limit;
}

cJSON *ocpp_profile_build_set_payload(int connector_id, float limit_amps,
                                       const char *purpose)
{
    static int s_profile_counter = 100;

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddNumberToObject(payload, "connectorId", connector_id);

    cJSON *profile = cJSON_CreateObject();
    cJSON_AddNumberToObject(profile, "chargingProfileId", s_profile_counter++);
    cJSON_AddNumberToObject(profile, "stackLevel", 0);
    cJSON_AddStringToObject(profile, "chargingProfilePurpose",
                            purpose ? purpose : "TxDefaultProfile");
    cJSON_AddStringToObject(profile, "chargingProfileKind", "Absolute");

    cJSON *schedule = cJSON_CreateObject();
    cJSON_AddStringToObject(schedule, "chargingRateUnit", "A");

    cJSON *periods = cJSON_CreateArray();
    cJSON *period = cJSON_CreateObject();
    cJSON_AddNumberToObject(period, "startPeriod", 0);
    cJSON_AddNumberToObject(period, "limit", limit_amps);
    cJSON_AddItemToArray(periods, period);

    cJSON_AddItemToObject(schedule, "chargingSchedulePeriod", periods);
    cJSON_AddItemToObject(profile, "chargingSchedule", schedule);
    cJSON_AddItemToObject(payload, "csChargingProfiles", profile);

    return payload;
}
