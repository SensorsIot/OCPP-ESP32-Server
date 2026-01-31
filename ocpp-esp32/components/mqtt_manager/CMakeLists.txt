idf_component_register(
    SRCS "mqtt_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt json config_manager led_status ocpp_server phase_control esp_event
)
