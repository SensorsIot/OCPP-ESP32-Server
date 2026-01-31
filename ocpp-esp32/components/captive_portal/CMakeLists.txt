idf_component_register(
    SRCS "captive_portal.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json dns_server ota_manager config_manager wifi_manager esp_timer esp_wifi
    EMBED_FILES "portal.html"
)
