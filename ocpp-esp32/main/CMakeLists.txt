idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        board_pins
        led_status
        config_manager
        gpio_control
        ethernet_manager
        wifi_manager
        captive_portal
        ocpp_server
        mqtt_manager
        phase_control
        console_cmd
        esp_event
        esp_netif
        esp_system
        nvs_flash
)
