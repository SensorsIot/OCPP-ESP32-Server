idf_component_register(
    SRCS "console_cmd.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins config_manager led_status gpio_control
             ethernet_manager wifi_manager
             console esp_system esp_wifi
)
