#pragma once

#include "soc/gpio_num.h"

/* WT32-ETH01 v1.4 - LAN8720 RMII Ethernet */
#define PIN_ETH_MDC     GPIO_NUM_23
#define PIN_ETH_MDIO    GPIO_NUM_18
#define PIN_ETH_PWR     GPIO_NUM_16   /* Oscillator enable (active high) */
#define ETH_PHY_ADDR    1

/* User Input (GPIO 14 — not a strapping pin, safe during reset) */
#define PIN_BTN_CONFIG  GPIO_NUM_14

/* Phase Switching Relay (single relay controls L2+L3 together; L1 always connected)
 * GPIO 4 — on WT32-ETH01 header; GPIO 25 is Ethernet RXD0 (RMII, not available) */
#define PIN_RELAY_PHASE23 GPIO_NUM_4
