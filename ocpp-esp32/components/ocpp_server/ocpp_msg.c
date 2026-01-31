#include "ocpp_msg.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "ocpp_msg";
static uint32_t s_msg_counter;

int ocpp_msg_parse(const char *data, int len, ocpp_parsed_msg_t *out)
{
    memset(out, 0, sizeof(*out));

    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGW(TAG, "Invalid OCPP message: not a JSON array");
        if (root) cJSON_Delete(root);
        return -1;
    }

    int arr_size = cJSON_GetArraySize(root);
    if (arr_size < 3) {
        ESP_LOGW(TAG, "OCPP message too short: %d elements", arr_size);
        cJSON_Delete(root);
        return -1;
    }

    cJSON *type_item = cJSON_GetArrayItem(root, 0);
    cJSON *id_item = cJSON_GetArrayItem(root, 1);

    if (!cJSON_IsNumber(type_item) || !cJSON_IsString(id_item)) {
        ESP_LOGW(TAG, "Invalid OCPP message type/id");
        cJSON_Delete(root);
        return -1;
    }

    out->type = (int)type_item->valuedouble;
    strlcpy(out->unique_id, id_item->valuestring, sizeof(out->unique_id));
    out->root = root;

    switch (out->type) {
    case OCPP_MSG_CALL:
        if (arr_size < 4) {
            cJSON_Delete(root);
            return -1;
        }
        {
            cJSON *action_item = cJSON_GetArrayItem(root, 2);
            if (!cJSON_IsString(action_item)) {
                cJSON_Delete(root);
                return -1;
            }
            strlcpy(out->action, action_item->valuestring, sizeof(out->action));
            out->payload = cJSON_GetArrayItem(root, 3);
        }
        break;
    case OCPP_MSG_CALLRESULT:
        out->payload = cJSON_GetArrayItem(root, 2);
        break;
    case OCPP_MSG_CALLERROR:
        if (arr_size >= 3) {
            cJSON *err_code = cJSON_GetArrayItem(root, 2);
            if (cJSON_IsString(err_code)) {
                strlcpy(out->action, err_code->valuestring, sizeof(out->action));
            }
        }
        out->payload = (arr_size >= 5) ? cJSON_GetArrayItem(root, 4) : NULL;
        break;
    default:
        ESP_LOGW(TAG, "Unknown OCPP message type: %d", out->type);
        cJSON_Delete(root);
        return -1;
    }

    return 0;
}

void ocpp_msg_free(ocpp_parsed_msg_t *msg)
{
    if (msg->root) {
        cJSON_Delete(msg->root);
        msg->root = NULL;
        msg->payload = NULL;
    }
}

void ocpp_msg_gen_id(char *buf, int len)
{
    s_msg_counter++;
    snprintf(buf, len, "srv-%lu-%lu",
             (unsigned long)(esp_timer_get_time() / 1000),
             (unsigned long)s_msg_counter);
}

char *ocpp_msg_call_result(const char *unique_id, cJSON *payload)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(OCPP_MSG_CALLRESULT));
    cJSON_AddItemToArray(arr, cJSON_CreateString(unique_id));
    cJSON_AddItemToArray(arr, payload ? payload : cJSON_CreateObject());

    char *str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return str;
}

char *ocpp_msg_call_error(const char *unique_id, const char *error_code,
                           const char *error_desc)
{
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(OCPP_MSG_CALLERROR));
    cJSON_AddItemToArray(arr, cJSON_CreateString(unique_id));
    cJSON_AddItemToArray(arr, cJSON_CreateString(error_code));
    cJSON_AddItemToArray(arr, cJSON_CreateString(error_desc ? error_desc : ""));
    cJSON_AddItemToArray(arr, cJSON_CreateObject());

    char *str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return str;
}

char *ocpp_msg_call(const char *action, cJSON *payload,
                     char *out_unique_id, int id_len)
{
    char uid[40];
    ocpp_msg_gen_id(uid, sizeof(uid));
    if (out_unique_id && id_len > 0) {
        strlcpy(out_unique_id, uid, id_len);
    }

    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(OCPP_MSG_CALL));
    cJSON_AddItemToArray(arr, cJSON_CreateString(uid));
    cJSON_AddItemToArray(arr, cJSON_CreateString(action));
    cJSON_AddItemToArray(arr, payload ? payload : cJSON_CreateObject());

    char *str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return str;
}
