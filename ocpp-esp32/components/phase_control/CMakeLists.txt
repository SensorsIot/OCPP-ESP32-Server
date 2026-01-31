idf_component_register(
    SRCS "phase_control.c"
    INCLUDE_DIRS "include"
    REQUIRES gpio_control ocpp_server mqtt_manager config_manager esp_timer
)
