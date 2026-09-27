// Advertising: OpenHaystack (Apple) and Google FMDN one-shot events, config mode
#include "adv.h"

#include <string.h>
#include "app_error.h"
#include "app_util.h"
#include "ble.h"
#include "ble_gap.h"
#include "settings.h"

#define APP_BLE_CONN_CFG_TAG   1
#define CONFIG_ADV_INTERVAL    MSEC_TO_UNITS(400, UNIT_0_625_MS)
// Only one event is sent per start, the interval just has to be valid
#define BEACON_ADV_INTERVAL    MSEC_TO_UNITS(100, UNIT_0_625_MS)

// Battery thresholds for a CR2032-class lithium cell (mV)
#define BATTERY_LEVEL_FULL     2900
#define BATTERY_LEVEL_NORMAL   2700
#define BATTERY_LEVEL_LOW      2500

static uint8_t m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
static bool m_next_is_fmdn;
static uint8_t m_status_apple;
static uint8_t m_status_fmdn;
static uint8_t m_status_counter;
static ble_gap_addr_t m_default_addr;

// Buffers must stay valid while advertising (SoftDevice reads them in place)
static uint8_t m_apple_adv[31];
static uint8_t m_fmdn_adv[29];
static uint8_t m_config_adv[3 + 2 + 8];
static uint8_t m_game_adv[31];
static uint8_t m_game_scanrsp[4];

static const int8_t m_tx_levels[] = { -8, 0, 4 };

static void build_apple(void)
{
    const uint8_t * key = g_settings.apple_key;
    static const uint8_t header[] = { 0x1e, 0xff, 0x4c, 0x00, 0x12, 0x19 };

    memcpy(m_apple_adv, header, sizeof(header));
    m_apple_adv[6] = m_status_apple;
    memcpy(&m_apple_adv[7], &key[6], 22);
    m_apple_adv[29] = key[0] >> 6;
    m_apple_adv[30] = 0x00;
}

static void build_fmdn(void)
{
    static const uint8_t header[] = { 0x02, 0x01, 0x06, 0x19, 0x16, 0xaa, 0xfe, 0x41 };

    memcpy(m_fmdn_adv, header, sizeof(header));
    memcpy(&m_fmdn_adv[8], g_settings.fmdn_key, FMDN_KEY_LEN);
    m_fmdn_adv[28] = m_status_fmdn;
}

// Random static address from the first 6 bytes of a key
static void addr_from_key(ble_gap_addr_t * addr, const uint8_t * key)
{
    addr->addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    addr->addr_id_peer = 0;
    addr->addr[5] = key[0] | 0xC0;
    addr->addr[4] = key[1];
    addr->addr[3] = key[2];
    addr->addr[2] = key[3];
    addr->addr[1] = key[4];
    addr->addr[0] = key[5];
}

static void set_tx_power(void)
{
    uint8_t level = g_settings.tx_power < sizeof(m_tx_levels) ? g_settings.tx_power : 1;
    (void) sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_ADV, m_adv_handle, m_tx_levels[level]);
}

static void start(ble_gap_addr_t const * addr, uint8_t * data, uint16_t len, bool connectable,
                  uint32_t interval, uint16_t duration, uint8_t max_evts)
{
    ble_gap_adv_params_t params = { 0 };
    ble_gap_adv_data_t adv_data = { 0 };

    adv_stop();
    APP_ERROR_CHECK(sd_ble_gap_addr_set(addr));

    params.properties.type = connectable ? BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED
                                         : BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED;
    params.interval = interval;
    params.duration = duration;
    params.max_adv_evts = max_evts;
    params.filter_policy = BLE_GAP_ADV_FP_ANY;
    params.primary_phy = BLE_GAP_PHY_1MBPS;

    adv_data.adv_data.p_data = data;
    adv_data.adv_data.len = len;

    APP_ERROR_CHECK(sd_ble_gap_adv_set_configure(&m_adv_handle, &adv_data, &params));
    set_tx_power();
    APP_ERROR_CHECK(sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG));
}

void adv_init(void)
{
    APP_ERROR_CHECK(sd_ble_gap_addr_get(&m_default_addr));
    build_apple();
    build_fmdn();
}

void adv_stop(void)
{
    if (m_adv_handle != BLE_GAP_ADV_SET_HANDLE_NOT_SET)
        (void) sd_ble_gap_adv_stop(m_adv_handle);
}

static bool apple_on(void)
{
    return g_settings.apple_enabled && settings_has_apple_key();
}

static bool fmdn_on(void)
{
    return g_settings.fmdn_enabled && settings_has_fmdn_key();
}

bool adv_anything_to_send(void)
{
    return apple_on() || fmdn_on();
}

void adv_beacon_tick(void)
{
    ble_gap_addr_t addr;
    bool fmdn;

    if (apple_on() && fmdn_on())
        fmdn = m_next_is_fmdn;
    else if (fmdn_on())
        fmdn = true;
    else if (apple_on())
        fmdn = false;
    else
        return;
    m_next_is_fmdn = !fmdn;

    if (fmdn)
    {
        addr_from_key(&addr, g_settings.fmdn_key);
        build_fmdn();
        start(&addr, m_fmdn_adv, sizeof(m_fmdn_adv), false, BEACON_ADV_INTERVAL, 0, 1);
    }
    else
    {
        addr_from_key(&addr, g_settings.apple_key);
        build_apple();
        // Connectable, so the app can ring/configure/update the tag at any time
        start(&addr, m_apple_adv, sizeof(m_apple_adv), true, BEACON_ADV_INTERVAL, 0, 1);
    }
}

// Status byte behaviour (compatible subset of Everytag's statusFlags):
//  bits 0-7   fixed Apple status byte     bits 8-15  fixed FMDN status byte
//  bits 16-19 Apple mode                  bits 20-23 FMDN mode
//  mode 1 = fixed byte, 2 = counter, 3/5 = voltage in 0.1 V, 4 = battery level OR fixed byte
void adv_update_status(uint16_t mv)
{
    uint32_t flags = g_settings.status_flags;
    uint8_t apple = flags & 0xff;
    uint8_t fmdn = (flags >> 8) & 0xff;
    uint8_t apple_mode = (flags >> 16) & 0xf;
    uint8_t fmdn_mode = (flags >> 20) & 0xf;
    uint8_t level;   // 0 full .. 3 critical

    if (mv > BATTERY_LEVEL_FULL)
        level = 0;
    else if (mv > BATTERY_LEVEL_NORMAL)
        level = 1;
    else if (mv > BATTERY_LEVEL_LOW)
        level = 2;
    else
        level = 3;
    m_status_counter++;

    switch (apple_mode)
    {
        case 2: apple = m_status_counter; break;
        case 3:
        case 5: apple = mv / 100; break;
        case 4: apple |= level << 6; break;
        default: break;
    }
    switch (fmdn_mode)
    {
        case 2: fmdn = m_status_counter; break;
        case 3:
        case 5: fmdn = mv / 100; break;
        // FMDN battery: 1 normal, 2 low, 3 critical
        case 4: fmdn |= (level <= 1 ? 1 : level) << 5; break;
        default: break;
    }
    m_status_apple = apple;
    m_status_fmdn = fmdn;
}

void adv_config_start(uint16_t timeout_s)
{
    static const uint8_t data[] = { 0x02, 0x01, 0x06, 0x09, 0x09, 'T', 'r', 'i', 'k', 'i', 'T', 'a', 'g' };
    ble_gap_addr_t addr = m_default_addr;
    const uint8_t * mac = g_settings.settings_mac;

    // Everytag convention: first and last byte non-zero = custom address
    if (mac[0] != 0 && mac[5] != 0)
    {
        memcpy(addr.addr, mac, 6);
        addr.addr[5] |= 0xC0;
        addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;
    }
    memcpy(m_config_adv, data, sizeof(data));
    start(&addr, m_config_adv, sizeof(data), true, CONFIG_ADV_INTERVAL, timeout_s * 100, 0);
}

void adv_game_start(const uint8_t mac[6], const uint8_t * name, uint8_t name_len, uint16_t timeout_s)
{
    ble_gap_adv_params_t params = { 0 };
    ble_gap_adv_data_t adv_data = { 0 };
    ble_gap_addr_t addr = { 0 };
    uint8_t n = 0;

    // Advertise the game identity as a random static address
    memcpy(addr.addr, mac, 6);
    addr.addr[5] |= 0xC0;
    addr.addr_type = BLE_GAP_ADDR_TYPE_RANDOM_STATIC;

    // Flags
    m_game_adv[n++] = 0x02;
    m_game_adv[n++] = 0x01;
    m_game_adv[n++] = 0x06;
    // Complete local name; trim so the manufacturer data still fits in 31 bytes
    if (name_len > 31 - 3 - 2 - 6)
        name_len = 31 - 3 - 2 - 6;
    m_game_adv[n++] = name_len + 1;
    m_game_adv[n++] = 0x09;
    memcpy(&m_game_adv[n], name, name_len);
    n += name_len;
    // Manufacturer data: company 0xFF00, payload a0 0a (as the original controller sends)
    m_game_adv[n++] = 0x05;
    m_game_adv[n++] = 0xFF;
    m_game_adv[n++] = 0x00;
    m_game_adv[n++] = 0xFF;
    m_game_adv[n++] = 0xA0;
    m_game_adv[n++] = 0x0A;

    // Scan response: complete list of 16-bit service UUIDs = 0x0001 (the app filters on it)
    m_game_scanrsp[0] = 0x03;
    m_game_scanrsp[1] = 0x03;
    m_game_scanrsp[2] = 0x01;
    m_game_scanrsp[3] = 0x00;

    adv_stop();
    APP_ERROR_CHECK(sd_ble_gap_addr_set(&addr));

    params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    params.interval = MSEC_TO_UNITS(100, UNIT_0_625_MS);
    params.duration = timeout_s * 100;
    params.max_adv_evts = 0;
    params.filter_policy = BLE_GAP_ADV_FP_ANY;
    params.primary_phy = BLE_GAP_PHY_1MBPS;

    adv_data.adv_data.p_data = m_game_adv;
    adv_data.adv_data.len = n;
    adv_data.scan_rsp_data.p_data = m_game_scanrsp;
    adv_data.scan_rsp_data.len = sizeof(m_game_scanrsp);

    APP_ERROR_CHECK(sd_ble_gap_adv_set_configure(&m_adv_handle, &adv_data, &params));
    set_tx_power();
    APP_ERROR_CHECK(sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG));
}
