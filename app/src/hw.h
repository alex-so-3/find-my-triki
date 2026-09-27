// Small hardware helpers for Zabka Triki: LED patterns, battery, SPI NOR sleep
#ifndef HW_H
#define HW_H

#include <stdint.h>
#include <stdbool.h>

void led_init(void);
// Blink `count` times: on_ms on, off_ms off (runs from app_timer, non-blocking)
void led_blink(uint8_t count, uint16_t on_ms, uint16_t off_ms);
void led_set(bool on);
// Stop any blink pattern and turn the LED off
void led_off(void);

// VDD in millivolts (SAADC, internal reference, gain 1/6)
uint16_t battery_mv(void);

// Put MX25R8035F into deep power-down (bit-banged, ~7 nA afterwards)
void nor_deep_power_down(void);

#endif
