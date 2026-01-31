#pragma once

#include "cJSON.h"
#include <stdint.h>
#include <stdbool.h>

#define MAX_CHARGING_PROFILES  8
#define MAX_SCHEDULE_PERIODS   10

typedef struct {
    int      start_period;
    float    limit;
    int      number_phases;
} ocpp_schedule_period_t;

typedef struct {
    int      profile_id;
    int      stack_level;
    char     purpose[24];       /* TxDefaultProfile, TxProfile, ChargePointMaxProfile */
    char     kind[16];          /* Absolute, Relative, Recurring */
    char     rate_unit[2];      /* A or W */
    int      num_periods;
    ocpp_schedule_period_t periods[MAX_SCHEDULE_PERIODS];
    int      connector_id;
    int      transaction_id;    /* -1 if not transaction-specific */
    bool     valid;
} ocpp_charging_profile_t;

/**
 * Store a charging profile. Replaces existing profile with same ID.
 */
void ocpp_profile_set(const cJSON *profile_json, int connector_id);

/**
 * Clear profiles matching criteria. Pass -1 or NULL to skip filtering.
 */
int ocpp_profile_clear(int profile_id, int connector_id,
                        const char *purpose, int stack_level);

/**
 * Get the current effective charging limit in Amps.
 * Returns the lowest applicable limit across all active profiles.
 * Returns -1.0 if no profiles are active.
 */
float ocpp_profile_get_effective_limit(int connector_id, int transaction_id);

/**
 * Build a SetChargingProfile payload cJSON object.
 * Caller must free with cJSON_Delete().
 */
cJSON *ocpp_profile_build_set_payload(int connector_id, float limit_amps,
                                       const char *purpose);
