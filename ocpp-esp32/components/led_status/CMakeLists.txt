idf_component_register(
    SRCS "led_status.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins esp_driver_gpio esp_timer
)
