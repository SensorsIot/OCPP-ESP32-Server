idf_component_register(
    SRCS "gpio_control.c"
    INCLUDE_DIRS "include"
    REQUIRES board_pins esp_driver_gpio esp_timer
)
