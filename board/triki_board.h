// Zabka Triki (nRF52810-QCAA) pinout.
// Source: https://github.com/Piwencjusz/zabka-triki-hardware (reverse engineered,
// partly guessed there; the pins below were checked on a real Triki, except the NOR flash).
#ifndef TRIKI_BOARD_H
#define TRIKI_BOARD_H

#include "nrf_gpio.h"

#define LEDS_NUMBER          1
#define LED_1                28
// LED is active low (confirmed on hardware)
#ifndef LEDS_ACTIVE_STATE
#define LEDS_ACTIVE_STATE    0
#endif
#define LEDS_LIST            { LED_1 }
#define LEDS_INV_MASK        LEDS_MASK
#define BSP_LED_0            LED_1

#define BUTTONS_NUMBER       1
#define BUTTON_1             25
#define BUTTON_PULL          NRF_GPIO_PIN_PULLUP
#define BUTTONS_ACTIVE_STATE 0
#define BUTTONS_LIST         { BUTTON_1 }
#define BSP_BUTTON_0         BUTTON_1

// LSM6DSL (I2C). CS high selects I2C; SA0 is strapped low on the board (address 0x6A).
// P0.04 is NOT the IMU's SA0, whatever the reverse-engineered schematic says: driving it high
// does not move the sensor to 0x6B, and driving it low costs ~60 uA through a ~45 kOhm pull-up
// on that net (measured with a PPK2). Leave it unconfigured.
#define IMU_SDA_PIN          5
#define IMU_SCL_PIN          6
#define IMU_INT1_PIN         9    // marked "???" on the schematic
#define IMU_INT2_PIN         10
#define IMU_CS_PIN           12
#define IMU_I2C_ADDR         0x6A

// MX25R8035F SPI NOR - only used to put it into deep power-down
#define NOR_CS_PIN           14
#define NOR_MISO_PIN         15
#define NOR_MOSI_PIN         20   // SCK and MOSI swapped vs. the reverse-engineered schematic:
#define NOR_SCK_PIN          18   // verified by reading the JEDEC ID C2 28 14

#endif
