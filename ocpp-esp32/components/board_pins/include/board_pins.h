#pragma once

#include "soc/gpio_num.h"

/* SPI Ethernet (W5500) */
#define PIN_ETH_MISO    GPIO_NUM_19
#define PIN_ETH_MOSI    GPIO_NUM_23
#define PIN_ETH_SCK     GPIO_NUM_18
#define PIN_ETH_CS      GPIO_NUM_5
#define PIN_ETH_INT     GPIO_NUM_4
#define PIN_ETH_RST     GPIO_NUM_21

#define ETH_SPI_HOST    SPI2_HOST
#define ETH_SPI_CLOCK_MHZ  20

/* Status LEDs */
#define PIN_LED_STATUS  GPIO_NUM_2
#define PIN_LED_OCPP    GPIO_NUM_15
#define PIN_LED_WIFI    GPIO_NUM_16
#define PIN_LED_MQTT    GPIO_NUM_17

/* User Input */
#define PIN_BTN_CONFIG  GPIO_NUM_0

/* Phase Switching Relays */
#define PIN_RELAY_PHASE1 GPIO_NUM_25
#define PIN_RELAY_PHASE2 GPIO_NUM_26
#define PIN_RELAY_PHASE3 GPIO_NUM_27

/* Phase Sense Feedback (input-only GPIO) */
#define PIN_PHASE_SENSE  GPIO_NUM_34
