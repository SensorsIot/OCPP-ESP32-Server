idf_component_register(
    SRCS "ota_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server app_update esp_app_format esp_partition esp_timer json
)
