// GATT configuration service, protocol compatible with Everytag's conn_beacon.py
#ifndef CONFIG_SVC_H
#define CONFIG_SVC_H

#include <stdbool.h>
#include "ble.h"

#ifndef APP_VERSION
#define APP_VERSION 0
#endif

// Live values reported through the info characteristic
typedef struct
{
    uint16_t battery_mv;
    bool     moving;
    bool     imu_ok;
} config_svc_runtime_t;

// Info characteristic (e9) payload, little-endian, 20 bytes
typedef struct __attribute__((packed))
{
    uint8_t  version;              // layout version, 1
    uint16_t app_version;
    uint16_t battery_mv;
    uint16_t still_timeout_s;
    uint16_t motion_threshold_mg;
    uint32_t status_flags;
    uint8_t  period;
    uint8_t  tx_power;
    uint8_t  apple_enabled;
    uint8_t  fmdn_enabled;
    uint8_t  moving;
    uint8_t  imu_ok;
} config_svc_info_t;

void config_svc_init(void);
void config_svc_set_runtime(config_svc_runtime_t const * runtime);
void config_svc_on_ble_evt(ble_evt_t const * p_ble_evt);

// Session state, reset on every connection
bool config_svc_changed(void);        // settings were modified and saved
bool config_svc_dfu_requested(void);  // client asked to reboot into the bootloader
bool config_svc_authorized(void);     // client wrote the correct password

#endif
