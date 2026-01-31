#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/**
 * Register OTA HTTP handlers on an existing HTTP server.
 * - GET  /api/ota/status  — returns firmware info as JSON
 * - POST /api/ota/upload  — receives firmware binary, performs OTA
 */
esp_err_t ota_manager_register_handlers(httpd_handle_t server);
