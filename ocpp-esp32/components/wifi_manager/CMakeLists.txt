idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins config_manager led_status
             esp_wifi esp_netif esp_event esp_timer lwip
)
