// Advertising: OpenHaystack (Apple) and Google FMDN one-shot events, config mode
#ifndef ADV_H
#define ADV_H

#include <stdint.h>
#include <stdbool.h>

void adv_init(void);
// Send one advertising event for the next enabled network (alternates); Apple events are
// connectable
void adv_beacon_tick(void);
// Recompute status bytes (battery etc.)
void adv_update_status(uint16_t battery_mv);
bool adv_anything_to_send(void);

// Connectable advertising for configuration/OTA, stops after timeout_s
void adv_config_start(uint16_t timeout_s);

// Connectable game-controller advertising under the given address (LSB first) and
// name, stops after timeout_s (0 = no timeout)
void adv_game_start(const uint8_t mac[6], const uint8_t * name, uint8_t name_len, uint16_t timeout_s);

void adv_stop(void);

#endif
