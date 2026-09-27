// Standard GATT services: DULT sound (play sound on the LED) and Nordic
// Buttonless DFU (unbonded), so generic apps can ring and update the tag
#ifndef STD_SVC_H
#define STD_SVC_H

#include <stdbool.h>
#include "ble.h"

typedef void (*std_svc_sound_handler_t)(bool start);

void std_svc_init(std_svc_sound_handler_t on_sound);
void std_svc_on_ble_evt(ble_evt_t const * p_ble_evt);

// A client asked to enter the DFU bootloader (Buttonless DFU), reset on disconnect
bool std_svc_dfu_requested(void);

#endif
