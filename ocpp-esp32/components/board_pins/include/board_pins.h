#pragma once

#include "soc/gpio_num.h"

/* WT32-ETH01 v1.4 - LAN8720 RMII Ethernet */
#define PIN_ETH_MDC     GPIO_NUM_23
#define PIN_ETH_MDIO    GPIO_NUM_18
#define PIN_ETH_PWR     GPIO_NUM_16   /* Oscillator enable (active high) */
#define ETH_PHY_ADDR    1

/* User Input */
#define PIN_BTN_CONFIG  GPIO_NUM_0

/* Phase Switching Relay (single relay controls L2+L3 together; L1 always connected) */
#define PIN_RELAY_PHASE23 GPIO_NUM_25

/* Phase Sense Feedback (input-only GPIO) */
#define PIN_PHASE_SENSE  GPIO_NUM_34
