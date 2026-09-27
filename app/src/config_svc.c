// GATT configuration service, protocol compatible with Everytag's conn_beacon.py
//
// Service 5cfce313-a7e3-45c3-933d-418b8100da7f, characteristics
// 8c5debXX-ad8d-4810-a31f-53862e79ee77 (write, 4-byte little-endian integers):
//   df auth (8 bytes; after a successful auth another write changes the password)
//   de Apple key, two 14-byte halves (only the first key is kept)
//   db FMDN on/off   dc Apple on/off   dd period 1/2/4/8   e1 TX power 0/1/2
//   e0 stationary timeout in seconds (Everytag: key change interval)
//   e2 FMDN EID (20 bytes)   e3 time (read/write, accepted but unused)
//   e4 config-mode MAC (6 bytes)   e5 status flags   e6 motion threshold in mg
//   e7 write 1 = reboot into the DFU bootloader (Triki extension)
//   ea game MAC (6 bytes, LSB first; all-zero = disable game mode) (Triki extension)
//   eb game advertised name (up to 20 bytes) (Triki extension)
//   e9 info: read-only after auth, see config_svc_info_t (Triki extension)
#include "config_svc.h"

#include <string.h>
#include "app_error.h"
#include "ble_gatts.h"
#include "settings.h"

// 5cfce313-a7e3-45c3-933d-418b8100da7f (bytes 12-13 replaced by the 16-bit alias)
static const ble_uuid128_t m_svc_base = { { 0x7f, 0xda, 0x00, 0x81, 0x8b, 0x41, 0x3d, 0x93,
                                             0xc3, 0x45, 0xe3, 0xa7, 0x00, 0x00, 0xfc, 0x5c } };
// 8c5debXX-ad8d-4810-a31f-53862e79ee77
static const ble_uuid128_t m_chr_base = { { 0x77, 0xee, 0x79, 0x2e, 0x86, 0x53, 0x1f, 0xa3,
                                             0x10, 0x48, 0x8d, 0xad, 0x00, 0x00, 0x5d, 0x8c } };

enum
{
    CHR_FMDN = 0xebdb, CHR_APPLE, CHR_PERIOD, CHR_KEY, CHR_AUTH,
    CHR_STILL = 0xebe0, CHR_TXPOWER, CHR_FMDN_KEY, CHR_TIME, CHR_MAC, CHR_STATUS, CHR_MOTION, CHR_DFU,
    CHR_INFO = 0xebe9, CHR_GAME_MAC = 0xebea, CHR_GAME_NAME = 0xebeb,
};

static const uint16_t m_chr_list[] =
{
    CHR_AUTH, CHR_KEY, CHR_PERIOD, CHR_FMDN, CHR_APPLE, CHR_STILL, CHR_TXPOWER,
    CHR_FMDN_KEY, CHR_TIME, CHR_MAC, CHR_STATUS, CHR_MOTION, CHR_DFU,
    CHR_GAME_MAC, CHR_GAME_NAME,
    CHR_INFO,   // info must stay last (read authorization finds it by index)
};
#define CHR_COUNT (sizeof(m_chr_list) / sizeof(m_chr_list[0]))

static uint16_t m_value_handles[CHR_COUNT];
static bool m_authorized;
static bool m_changed;
static bool m_dfu;
static uint8_t m_key_halves;
static config_svc_runtime_t m_runtime;

static void add_char(uint16_t svc_handle, uint8_t chr_type, int idx)
{
    ble_gatts_char_md_t char_md = { 0 };
    ble_gatts_attr_md_t attr_md = { 0 };
    ble_gatts_attr_t attr = { 0 };
    ble_gatts_char_handles_t handles;
    ble_uuid_t uuid = { .uuid = m_chr_list[idx], .type = chr_type };
    bool info = (m_chr_list[idx] == CHR_INFO);
    bool readable = (m_chr_list[idx] == CHR_TIME) || info;

    char_md.char_props.write = !info;
    char_md.char_props.read = readable;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    if (readable)
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    else
        BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.read_perm);
    // Info is served only to an authorized client (read authorization)
    attr_md.rd_auth = info;
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr_md.vlen = 1;

    attr.p_uuid = &uuid;
    attr.p_attr_md = &attr_md;
    attr.init_len = readable ? 8 : 0;
    attr.max_len = 20;

    APP_ERROR_CHECK(sd_ble_gatts_characteristic_add(svc_handle, &char_md, &attr, &handles));
    m_value_handles[idx] = handles.value_handle;
}

void config_svc_init(void)
{
    uint8_t svc_type, chr_type;
    uint16_t svc_handle;
    ble_uuid_t svc_uuid;

    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&m_svc_base, &svc_type));
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&m_chr_base, &chr_type));
    svc_uuid.type = svc_type;
    svc_uuid.uuid = 0xe313;
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &svc_uuid, &svc_handle));
    for (int i = 0; i < CHR_COUNT; i++)
        add_char(svc_handle, chr_type, i);
}

static uint32_t le32(const uint8_t * p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static void on_write(uint16_t chr, const uint8_t * data, uint16_t len)
{
    settings_t * s = &g_settings;
    uint32_t v = (len == 4) ? le32(data) : 0;
    bool changed = true;

    if (chr == CHR_AUTH)
    {
        if (len != AUTH_LEN)
            return;
        if (m_authorized)
            memcpy(s->auth, data, AUTH_LEN);   // password change
        else
        {
            m_authorized = (memcmp(s->auth, data, AUTH_LEN) == 0);
            m_key_halves = 0;
            return;
        }
    }
    else if (!m_authorized)
        return;
    else if (chr == CHR_KEY && len == 14)
    {
        // Only the first key (halves 0 and 1) is used, the rest is ignored
        if (m_key_halves < 2)
            memcpy(&s->apple_key[14 * m_key_halves], data, 14);
        else
            changed = false;
        m_key_halves++;
    }
    else if (chr == CHR_FMDN && len == 4)
        s->fmdn_enabled = v ? 1 : 0;
    else if (chr == CHR_APPLE && len == 4)
        s->apple_enabled = v ? 1 : 0;
    else if (chr == CHR_PERIOD && len == 4 && (v == 1 || v == 2 || v == 4 || v == 8))
        s->period = v;
    else if (chr == CHR_TXPOWER && len == 4 && v <= 2)
        s->tx_power = v;
    else if (chr == CHR_STILL && len == 4 && v >= 30 && v <= 7200)
        s->still_timeout_s = v;
    else if (chr == CHR_FMDN_KEY && len == FMDN_KEY_LEN)
        memcpy(s->fmdn_key, data, FMDN_KEY_LEN);
    else if (chr == CHR_MAC && len == 6)
        memcpy(s->settings_mac, data, 6);
    else if (chr == CHR_STATUS && len == 4)
        s->status_flags = v;
    else if (chr == CHR_MOTION && len == 4 && v < 2000)
        s->motion_threshold_mg = v;
    else if (chr == CHR_GAME_MAC && len == 6)
        memcpy(s->game_mac, data, 6);
    else if (chr == CHR_GAME_NAME && len <= GAME_NAME_MAX)
    {
        memcpy(s->game_name, data, len);
        s->game_name_len = (uint8_t) len;
    }
    else if (chr == CHR_DFU && len >= 1 && data[0] == 1)
    {
        m_dfu = true;
        changed = false;
    }
    else
        changed = false;

    if (changed)
        m_changed = true;
}

static void on_info_read(uint16_t conn_handle)
{
    settings_t const * s = &g_settings;
    config_svc_info_t info =
    {
        .version = 1,
        .app_version = APP_VERSION,
        .battery_mv = m_runtime.battery_mv,
        .still_timeout_s = s->still_timeout_s,
        .motion_threshold_mg = s->motion_threshold_mg,
        .status_flags = s->status_flags,
        .period = s->period,
        .tx_power = s->tx_power,
        .apple_enabled = s->apple_enabled,
        .fmdn_enabled = s->fmdn_enabled,
        .moving = m_runtime.moving,
        .imu_ok = m_runtime.imu_ok,
    };
    ble_gatts_rw_authorize_reply_params_t reply = { .type = BLE_GATTS_AUTHORIZE_TYPE_READ };

    if (m_authorized)
    {
        reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
        reply.params.read.update = 1;
        reply.params.read.p_data = (uint8_t const *) &info;
        reply.params.read.len = sizeof(info);
    }
    else
        reply.params.read.gatt_status = BLE_GATT_STATUS_ATTERR_READ_NOT_PERMITTED;
    (void) sd_ble_gatts_rw_authorize_reply(conn_handle, &reply);
}

void config_svc_on_ble_evt(ble_evt_t const * p_ble_evt)
{
    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_authorized = false;
            m_changed = false;
            m_dfu = false;
            m_key_halves = 0;
            break;

        case BLE_GATTS_EVT_WRITE:
        {
            ble_gatts_evt_write_t const * w = &p_ble_evt->evt.gatts_evt.params.write;
            for (int i = 0; i < CHR_COUNT; i++)
                if (w->handle == m_value_handles[i])
                    on_write(m_chr_list[i], w->data, w->len);
            break;
        }

        case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
        {
            ble_gatts_evt_rw_authorize_request_t const * r =
                &p_ble_evt->evt.gatts_evt.params.authorize_request;
            if (r->type == BLE_GATTS_AUTHORIZE_TYPE_READ &&
                r->request.read.handle == m_value_handles[CHR_COUNT - 1])
                on_info_read(p_ble_evt->evt.gatts_evt.conn_handle);
            break;
        }

        default:
            break;
    }
}

bool config_svc_changed(void)
{
    return m_changed;
}

bool config_svc_dfu_requested(void)
{
    return m_dfu;
}

bool config_svc_authorized(void)
{
    return m_authorized;
}

void config_svc_set_runtime(config_svc_runtime_t const * runtime)
{
    m_runtime = *runtime;
}
