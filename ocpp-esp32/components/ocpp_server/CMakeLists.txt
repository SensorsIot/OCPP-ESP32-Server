idf_component_register(
    SRCS "ocpp_server.c" "ocpp_msg.c" "ocpp_session.c" "ocpp_charging_profile.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server json config_manager led_status gpio_control esp_timer esp_event esp_netif
)
