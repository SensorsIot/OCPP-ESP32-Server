idf_component_register(
    SRCS "ethernet_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins config_manager led_status
             esp_driver_gpio esp_driver_spi esp_eth esp_netif esp_event
)
