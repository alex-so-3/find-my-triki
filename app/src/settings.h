// Persistent settings (FDS), layout independent of the GATT protocol
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

#define APPLE_KEY_LEN 28
#define FMDN_KEY_LEN  20
#define AUTH_LEN      8

typedef struct
{
    uint32_t magic;
    uint8_t  apple_key[APPLE_KEY_LEN];  // OpenHaystack public key
    uint8_t  fmdn_key[FMDN_KEY_LEN];    // Google FMDN EID (precomputed)
    uint8_t  auth[AUTH_LEN];            // password for the config service
    uint8_t  settings_mac[6];           // address in config mode (LSB first), 0 = default
    uint8_t  apple_enabled;
    uint8_t  fmdn_enabled;
    uint8_t  period;                    // 1, 2, 4, 8 - multiplier of the base interval
    uint8_t  tx_power;                  // 0 = -8 dBm, 1 = 0 dBm, 2 = +4 dBm
    uint16_t still_timeout_s;           // switch to slow advertising after this time without motion
    uint16_t motion_threshold_mg;       // 0 = IMU off, always "moving"
    uint32_t status_flags;              // status byte behaviour, see adv.c
    // --- appended fields: older records load with these zeroed (see settings_init) ---
    uint8_t  game_mac[6];               // BLE address (LSB first) for game mode; all-zero = off
    uint8_t  game_name_len;             // advertised name length in game mode
    uint8_t  game_name[20];             // advertised name in game mode ("Triki 1234567890")
} settings_t;

#define GAME_NAME_MAX 20

extern settings_t g_settings;

// Load from flash (blocking until FDS is ready), defaults if nothing stored
void settings_init(void);
// Queue a write of g_settings; completes asynchronously
void settings_save(void);
bool settings_busy(void);

bool settings_has_apple_key(void);
bool settings_has_fmdn_key(void);
// True when a game-mode identity (MAC) is configured
bool settings_has_game(void);

#endif
