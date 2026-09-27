// Standard GATT services
//
// DULT non-owner service (IETF draft "Detecting Unwanted Location Trackers"):
//   service 15190001-12f4-c226-88ed-2ac5579f2a85, control point
//   8e0c0001-1d68-fb92-bf61-48377421680e (write, indicate). Opcodes are
//   little-endian: 0x0300 Sound_Start, 0x0301 Sound_Stop; the tag answers with
//   0x0302 Command_Response <opcode> <status 0x0000>. The "sound" is the LED.
//
// Nordic Buttonless DFU without bonds: service 0xfe59, characteristic
//   8ec90003-f315-4f60-9fb8-838830daea50 (write, indicate). 0x01 = enter the
//   bootloader, answered with 0x20 0x01 0x01; the tag then disconnects and
//   restarts into the DFU bootloader (which advertises service 0xfe59).
#include "std_svc.h"

#include <string.h>
#include "app_error.h"
#include "ble_gatts.h"
#include "ble_hci.h"

#define DULT_SOUND_START      0x0300
#define DULT_SOUND_STOP       0x0301
#define DULT_COMMAND_RESPONSE 0x0302
#define DULT_STATUS_SUCCESS   0x0000
#define DULT_STATUS_INVALID   0x0003   // invalid command

#define DFU_OP_ENTER          0x01
#define DFU_OP_RESPONSE       0x20
#define DFU_RES_SUCCESS       0x01
#define DFU_RES_NOT_SUPPORTED 0x02

// 15190001-12f4-c226-88ed-2ac5579f2a85 (bytes 12-13 are the 16-bit alias)
static const ble_uuid128_t m_dult_svc_base = { { 0x85, 0x2a, 0x9f, 0x57, 0xc5, 0x2a, 0xed, 0x88,
                                                  0x26, 0xc2, 0xf4, 0x12, 0x00, 0x00, 0x19, 0x15 } };
// 8e0c0001-1d68-fb92-bf61-48377421680e
static const ble_uuid128_t m_dult_chr_base = { { 0x0e, 0x68, 0x21, 0x74, 0x37, 0x48, 0x61, 0xbf,
                                                  0x92, 0xfb, 0x68, 0x1d, 0x00, 0x00, 0x0c, 0x8e } };
// 8ec90003-f315-4f60-9fb8-838830daea50
static const ble_uuid128_t m_dfu_chr_base = { { 0x50, 0xea, 0xda, 0x30, 0x88, 0x83, 0xb8, 0x9f,
                                                 0x60, 0x4f, 0x15, 0xf3, 0x00, 0x00, 0xc9, 0x8e } };

static ble_gatts_char_handles_t m_dult_cp;
static ble_gatts_char_handles_t m_dfu_cp;
static std_svc_sound_handler_t m_on_sound;
static bool m_dfu_pending;     // waiting for the indication to be confirmed
static bool m_dfu_requested;

static void add_control_point(uint16_t svc_handle, ble_uuid_t const * uuid, ble_gatts_char_handles_t * handles)
{
    ble_gatts_char_md_t char_md = { 0 };
    ble_gatts_attr_md_t cccd_md = { 0 };
    ble_gatts_attr_md_t attr_md = { 0 };
    ble_gatts_attr_t attr = { 0 };

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
    cccd_md.vloc = BLE_GATTS_VLOC_STACK;

    char_md.char_props.write = 1;
    char_md.char_props.indicate = 1;
    char_md.p_cccd_md = &cccd_md;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.read_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr_md.vlen = 1;

    attr.p_uuid = uuid;
    attr.p_attr_md = &attr_md;
    attr.max_len = 20;

    APP_ERROR_CHECK(sd_ble_gatts_characteristic_add(svc_handle, &char_md, &attr, handles));
}

void std_svc_init(std_svc_sound_handler_t on_sound)
{
    uint16_t svc_handle;
    ble_uuid_t uuid;

    m_on_sound = on_sound;

    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&m_dult_svc_base, &uuid.type));
    uuid.uuid = 0x0001;
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &uuid, &svc_handle));
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&m_dult_chr_base, &uuid.type));
    uuid.uuid = 0x0001;
    add_control_point(svc_handle, &uuid, &m_dult_cp);

    BLE_UUID_BLE_ASSIGN(uuid, 0xfe59);
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &uuid, &svc_handle));
    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&m_dfu_chr_base, &uuid.type));
    uuid.uuid = 0x0003;
    add_control_point(svc_handle, &uuid, &m_dfu_cp);
}

static bool indications_on(uint16_t conn_handle, uint16_t cccd_handle)
{
    uint8_t cccd[2] = { 0 };
    ble_gatts_value_t value = { .len = sizeof(cccd), .offset = 0, .p_value = cccd };

    if (sd_ble_gatts_value_get(conn_handle, cccd_handle, &value) != NRF_SUCCESS)
        return false;
    return (cccd[0] & BLE_GATT_HVX_INDICATION) != 0;
}

// Errors are ignored: the client may already be gone (the app disconnects
// right after its write succeeds)
static bool indicate(uint16_t conn_handle, ble_gatts_char_handles_t const * cp, uint8_t * data, uint16_t len)
{
    ble_gatts_hvx_params_t hvx = { 0 };

    if (!indications_on(conn_handle, cp->cccd_handle))
        return false;
    hvx.handle = cp->value_handle;
    hvx.type = BLE_GATT_HVX_INDICATION;
    hvx.p_len = &len;
    hvx.p_data = data;
    return sd_ble_gatts_hvx(conn_handle, &hvx) == NRF_SUCCESS;
}

static void on_dult_write(uint16_t conn_handle, uint8_t const * data, uint16_t len)
{
    uint16_t opcode = (len >= 2) ? (uint16_t)(data[0] | (data[1] << 8)) : 0;
    uint16_t status = DULT_STATUS_SUCCESS;

    if (opcode == DULT_SOUND_START)
        m_on_sound(true);
    else if (opcode == DULT_SOUND_STOP)
        m_on_sound(false);
    else
        status = DULT_STATUS_INVALID;

    uint8_t rsp[6] = { DULT_COMMAND_RESPONSE & 0xff, DULT_COMMAND_RESPONSE >> 8,
                       opcode & 0xff, opcode >> 8, status & 0xff, status >> 8 };
    (void) indicate(conn_handle, &m_dult_cp, rsp, sizeof(rsp));
}

static void on_dfu_write(uint16_t conn_handle, uint8_t const * data, uint16_t len)
{
    uint8_t op = len ? data[0] : 0;
    uint8_t rsp[3] = { DFU_OP_RESPONSE, op, DFU_RES_NOT_SUPPORTED };

    if (op != DFU_OP_ENTER)
    {
        (void) indicate(conn_handle, &m_dfu_cp, rsp, sizeof(rsp));
        return;
    }
    m_dfu_requested = true;
    rsp[2] = DFU_RES_SUCCESS;
    // Disconnect once the client confirmed the response; without indications
    // enabled there is nothing to wait for
    if (indicate(conn_handle, &m_dfu_cp, rsp, sizeof(rsp)))
        m_dfu_pending = true;
    else
        (void) sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}

void std_svc_on_ble_evt(ble_evt_t const * p_ble_evt)
{
    uint16_t conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_dfu_pending = false;
            m_dfu_requested = false;
            break;

        case BLE_GATTS_EVT_WRITE:
        {
            ble_gatts_evt_write_t const * w = &p_ble_evt->evt.gatts_evt.params.write;
            if (w->handle == m_dult_cp.value_handle)
                on_dult_write(conn_handle, w->data, w->len);
            else if (w->handle == m_dfu_cp.value_handle)
                on_dfu_write(conn_handle, w->data, w->len);
            break;
        }

        case BLE_GATTS_EVT_HVC:
            if (m_dfu_pending && p_ble_evt->evt.gatts_evt.params.hvc.handle == m_dfu_cp.value_handle)
            {
                m_dfu_pending = false;
                (void) sd_ble_gap_disconnect(conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            }
            break;

        default:
            break;
    }
}

bool std_svc_dfu_requested(void)
{
    return m_dfu_requested;
}
