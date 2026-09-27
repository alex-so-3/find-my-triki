// Optional game-controller mode: streams the LSM6DSL over a Nordic UART Service so
// motion-controller software (e.g. the Triki app, TrikiVR/SlimeVR) can use the tag.
// Entered on a double click and left on disconnect; costs nothing when unused.
#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "ble.h"

// Add the NUS / Battery / Device Information services (once, at boot).
void game_init(void);

// Allow (or forbid) streaming. Only true while the tag is in game mode, so a stray
// write to the always-present NUS from another connection cannot start the sensor.
void game_set_active(bool active);

// Called for every BLE event while a connection is up (conn_handle from main).
void game_on_ble_evt(ble_evt_t const * p_ble_evt, uint16_t conn_handle);

// Stop streaming and restore the sensor's low-power wake-up configuration.
void game_stop(void);

// True while IMU frames are being streamed to a connected client.
bool game_streaming(void);

#endif
