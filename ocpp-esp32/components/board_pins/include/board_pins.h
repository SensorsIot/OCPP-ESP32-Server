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

/* User Input */
#define PIN_BTN_CONFIG  GPIO_NUM_0

/* Phase Switching Relay (single relay controls L2+L3 together; L1 always connected) */
#define PIN_RELAY_PHASE23 GPIO_NUM_25

/* Phase Sense Feedback (input-only GPIO) */
#define PIN_PHASE_SENSE  GPIO_NUM_34
