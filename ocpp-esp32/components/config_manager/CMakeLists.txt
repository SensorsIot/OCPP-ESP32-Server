idf_component_register(
    SRCS "config_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins nvs_flash esp_wifi json
)
