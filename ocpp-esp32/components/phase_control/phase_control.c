#include "phase_control.h"
#include "gpio_control.h"
#include "ocpp_server.h"
#include "ocpp_charging_profile.h"
#include "mqtt_manager.h"
#include "config_manager.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <math.h>

static const char *TAG = "phase_ctrl";

#define PHASE_SWITCH_TIMEOUT_US    (30ULL * 1000 * 1000) /* 30s */

static inline int64_t get_switch_delay_us(void) {
    return (int64_t)config_get()->phase_switch_delay * 1000;
}
static inline float get_threshold_kw(void) {
    return (float)config_get()->phase_switch_threshold / 1000.0f;
}
static inline float get_max_current(phase_mode_t mode) {
    const config_t *cfg = config_get();
    uint16_t raw = (mode == PHASE_MODE_1) ? cfg->phase_1_max_current : cfg->phase_3_max_current;
    return (float)raw / 10.0f;
}

static phase_switch_state_t s_state = PHASE_SW_IDLE;
static phase_mode_t s_target_mode;
static int64_t s_state_enter_time;
static esp_timer_handle_t s_timer;
static char s_saved_id_tag[21];
static int s_saved_transaction_id;
static int64_t s_switch_start_time;
static bool s_verify_pending;  /* true after relay switch until MeterValues confirm */

static void enter_state(phase_switch_state_t new_state)
{
    ESP_LOGI(TAG, "Phase switch state: %d → %d", s_state, new_state);
    s_state = new_state;
    s_state_enter_time = esp_timer_get_time();
}

static void phase_switch_complete(bool success, const char *error)
{
    const char *old_mode = (s_target_mode == PHASE_MODE_1) ? "3-phase" : "1-phase";
    const char *new_mode = (s_target_mode == PHASE_MODE_1) ? "1-phase" : "3-phase";
    int duration_ms = (int)((esp_timer_get_time() - s_switch_start_time) / 1000);

    if (success) {
        ESP_LOGI(TAG, "Phase switch complete: %s → %s (%d ms)",
                 old_mode, new_mode, duration_ms);
    } else {
        ESP_LOGE(TAG, "Phase switch FAILED: %s (duration %d ms)",
                 error ? error : "unknown", duration_ms);
    }

    mqtt_publish_phase_result(success, old_mode, new_mode, duration_ms, error);
    enter_state(PHASE_SW_IDLE);
}

/* Timer callback for state machine progression */
static void phase_timer_cb(void *arg)
{
    (void)arg;
    int64_t elapsed = esp_timer_get_time() - s_state_enter_time;

    switch (s_state) {
    case PHASE_SW_STOPPING:
        if (elapsed > PHASE_SWITCH_TIMEOUT_US) {
            phase_switch_complete(false, "timeout waiting for stop");
        }
        break;

    case PHASE_SW_WAITING:
        if (elapsed > PHASE_SWITCH_TIMEOUT_US) {
            phase_switch_complete(false, "timeout waiting for Available");
        }
        break;

    case PHASE_SW_DELAY:
        if (elapsed > get_switch_delay_us()) {
            /* Perform the actual relay switch */
            enter_state(PHASE_SW_SWITCHING);
            gpio_set_phase_mode(s_target_mode);
            s_verify_pending = true;

            /* If there was an active transaction, restart it.
             * Phase verification happens asynchronously via MeterValues —
             * once charging resumes, the wallbox reports per-phase voltage
             * and phase_control_on_meter_values() checks L2/L3. */
            if (s_saved_id_tag[0]) {
                enter_state(PHASE_SW_STARTING);
                ocpp_send_remote_start(1, s_saved_id_tag);
            } else {
                phase_switch_complete(true, NULL);
            }
        }
        break;

    case PHASE_SW_STARTING:
        if (elapsed > PHASE_SWITCH_TIMEOUT_US) {
            /* Transaction restart failed — but switch itself succeeded */
            ESP_LOGW(TAG, "Transaction restart timed out after phase switch");
            phase_switch_complete(true, NULL);
        }
        break;

    default:
        break;
    }
}

/* Voltage threshold — below this, phase is considered disconnected */
#define VOLTAGE_PRESENT_THRESHOLD  50.0f   /* volts */

/* OCPP callback: meter values — verify phase voltage after a switch */
static void on_ocpp_meter(int connector_id, int transaction_id,
                           const cJSON *values)
{
    if (!s_verify_pending) return;

    const ocpp_session_t *sess = ocpp_server_get_session();

    /* Skip if wallbox doesn't report per-phase voltage yet */
    if (sess->voltage_l2 == 0.0f && sess->voltage_l3 == 0.0f &&
        sess->voltage_l1 == 0.0f) {
        return;
    }

    bool l2_present = sess->voltage_l2 > VOLTAGE_PRESENT_THRESHOLD;
    bool l3_present = sess->voltage_l3 > VOLTAGE_PRESENT_THRESHOLD;
    phase_mode_t current = gpio_get_phase_mode();

    if (current == PHASE_MODE_1 && (l2_present || l3_present)) {
        ESP_LOGE(TAG, "Voltage mismatch! 1-phase mode but L2=%.0fV L3=%.0fV",
                 sess->voltage_l2, sess->voltage_l3);
        mqtt_publish_phase_result(false,
            "1-phase", "1-phase", 0, "voltage_mismatch");
    } else if (current == PHASE_MODE_3 && (!l2_present || !l3_present)) {
        ESP_LOGE(TAG, "Voltage mismatch! 3-phase mode but L2=%.0fV L3=%.0fV",
                 sess->voltage_l2, sess->voltage_l3);
        mqtt_publish_phase_result(false,
            "3-phase", "3-phase", 0, "voltage_mismatch");
    } else {
        ESP_LOGI(TAG, "Phase voltage verified OK (L2=%.0fV L3=%.0fV)",
                 sess->voltage_l2, sess->voltage_l3);
    }

    s_verify_pending = false;
}

/* OCPP callback: status change */
static void on_ocpp_status(int connector_id, ocpp_status_t status,
                            const char *error_code)
{
    phase_control_notify_status(connector_id, ocpp_status_str(status));
}

/* OCPP callback: session start/stop */
static void on_ocpp_session(const ocpp_session_t *session, bool started)
{
    if (!started) {
        phase_control_notify_transaction_stopped();
    }
}

esp_err_t phase_control_init(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = phase_timer_cb,
        .name = "phase_sw",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, 500 * 1000)); /* 500ms poll */

    /* Register for OCPP notifications */
    ocpp_server_set_status_cb(on_ocpp_status);
    ocpp_server_set_session_cb(on_ocpp_session);
    ocpp_server_set_meter_cb(on_ocpp_meter);

    ESP_LOGI(TAG, "Phase control initialised (mode=%s)",
             gpio_get_phase_mode() == PHASE_MODE_3 ? "3-phase" : "1-phase");
    return ESP_OK;
}

esp_err_t phase_control_request_switch(phase_mode_t target)
{
    if (s_state != PHASE_SW_IDLE) {
        ESP_LOGW(TAG, "Phase switch already in progress");
        return ESP_ERR_INVALID_STATE;
    }

    if (target == gpio_get_phase_mode()) {
        ESP_LOGI(TAG, "Already in requested phase mode");
        return ESP_OK;
    }

    s_target_mode = target;
    s_switch_start_time = esp_timer_get_time();

    /* Save current session info */
    const ocpp_session_t *sess = ocpp_server_get_session();
    if (sess->active) {
        strlcpy(s_saved_id_tag, sess->id_tag, sizeof(s_saved_id_tag));
        s_saved_transaction_id = sess->transaction_id;

        /* Step 1: Stop active transaction */
        enter_state(PHASE_SW_STOPPING);
        ocpp_send_remote_stop(sess->transaction_id);
        ESP_LOGI(TAG, "Stopping transaction %d before phase switch", sess->transaction_id);
    } else {
        s_saved_id_tag[0] = '\0';
        /* No active transaction — check if status is safe */
        if (sess->status == OCPP_STATUS_AVAILABLE ||
            sess->status == OCPP_STATUS_FINISHING) {
            enter_state(PHASE_SW_DELAY);
        } else {
            enter_state(PHASE_SW_WAITING);
        }
    }

    return ESP_OK;
}

phase_switch_state_t phase_control_get_state(void)
{
    return s_state;
}

phase_mode_t phase_control_get_mode(void)
{
    return gpio_get_phase_mode();
}

esp_err_t phase_control_set_power_limit(float power_kw)
{
    phase_mode_t current = gpio_get_phase_mode();
    phase_mode_t target;

    if (power_kw < get_threshold_kw()) {
        target = PHASE_MODE_1;
    } else {
        target = PHASE_MODE_3;
    }

    if (target != current && s_state == PHASE_SW_IDLE) {
        ESP_LOGI(TAG, "Power limit %.1f kW → switching to %s",
                 power_kw, target == PHASE_MODE_1 ? "1-phase" : "3-phase");
        return phase_control_request_switch(target);
    }

    /* Apply current limit via charging profile */
    float amps;
    if (target == PHASE_MODE_1) {
        amps = (float)(power_kw * 1000.0 / 230.0);
    } else {
        amps = (float)(power_kw * 1000.0 / (3.0 * 230.0));
    }

    float max_a = get_max_current(target);
    if (amps > max_a) amps = max_a;
    if (amps < 0) amps = 0;

    cJSON *profile_payload = ocpp_profile_build_set_payload(1, amps, "TxDefaultProfile");
    cJSON *cs = cJSON_DetachItemFromObject(profile_payload, "csChargingProfiles");
    if (cs) {
        ocpp_send_set_charging_profile(1, cs);
        cJSON_Delete(cs);
    }
    cJSON_Delete(profile_payload);

    return ESP_OK;
}

float phase_control_get_correction_factor(void)
{
    return (gpio_get_phase_mode() == PHASE_MODE_1) ? 3.0f : 1.0f;
}

void phase_control_notify_status(int connector_id, const char *status)
{
    if (s_state == PHASE_SW_WAITING) {
        if (strcmp(status, "Available") == 0 || strcmp(status, "Finishing") == 0) {
            ESP_LOGI(TAG, "Charger is %s — entering safety delay", status);
            enter_state(PHASE_SW_DELAY);
        }
    }
}

void phase_control_notify_transaction_stopped(void)
{
    if (s_state == PHASE_SW_STOPPING) {
        ESP_LOGI(TAG, "Transaction stopped — waiting for Available status");
        enter_state(PHASE_SW_WAITING);
    }
}
