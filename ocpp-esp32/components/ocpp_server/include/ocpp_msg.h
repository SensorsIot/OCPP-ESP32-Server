#pragma once

#include "cJSON.h"
#include <stdint.h>

/* OCPP-J message types */
#define OCPP_MSG_CALL        2
#define OCPP_MSG_CALLRESULT  3
#define OCPP_MSG_CALLERROR   4

/* Parsed OCPP message */
typedef struct {
    int     type;           /* 2=Call, 3=CallResult, 4=CallError */
    char    unique_id[40];
    char    action[40];     /* only for Call */
    cJSON  *payload;        /* caller must NOT free — owned by parsed root */
    cJSON  *root;           /* the full parsed array — caller frees */
} ocpp_parsed_msg_t;

/**
 * Parse an OCPP-J message string.
 * Returns 0 on success. Caller must call ocpp_msg_free() when done.
 */
int ocpp_msg_parse(const char *data, int len, ocpp_parsed_msg_t *out);

/**
 * Free a parsed message.
 */
void ocpp_msg_free(ocpp_parsed_msg_t *msg);

/**
 * Build a CallResult response.
 * Returns malloc'd string (caller frees).
 */
char *ocpp_msg_call_result(const char *unique_id, cJSON *payload);

/**
 * Build a CallError response.
 * Returns malloc'd string (caller frees).
 */
char *ocpp_msg_call_error(const char *unique_id, const char *error_code,
                           const char *error_desc);

/**
 * Build a Call message (server → charger).
 * Returns malloc'd string (caller frees). unique_id is auto-generated.
 */
char *ocpp_msg_call(const char *action, cJSON *payload, char *out_unique_id, int id_len);

/**
 * Generate a unique message ID.
 */
void ocpp_msg_gen_id(char *buf, int len);
