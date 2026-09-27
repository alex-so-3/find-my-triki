// Game-controller mode (see game.h). Protocol reverse-engineered by the TrikiEmu
// project (github.com/Maku-hub/TrikiEmu, MIT) and TrikiVR: a Nordic UART Service
// carries START/STOP on RX and a continuous stream of 14-byte IMU frames on TX.
#include "game.h"

#include <string.h>
#include "app_error.h"
#include "app_timer.h"
#include "ble_gatts.h"
#include "ble_srv_common.h"

#include "boards.h"
#include "nrf_gpio.h"

#include "hw.h"
#include "imu.h"
#include "settings.h"

// Nordic UART Service base 6e400001-b5a3-f393-e0a9-e50e24dcca9e (16-bit alias zeroed)
static const ble_uuid128_t m_nus_base = { { 0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                                            0x93, 0xf3, 0xa3, 0xb5, 0x00, 0x00, 0x40, 0x6e } };
#define NUS_UUID_SVC      0x0001
#define NUS_UUID_RX       0x0002   // phone -> tag (write / write-without-response)
#define NUS_UUID_TX       0x0003   // tag -> phone (notify)
#define NUS_UUID_VENDOR   0x0004   // read/write, bit0 = LED

// 14-byte IMU frame: 0x22, status, then gyro XYZ + accel XYZ (int16 LE) read straight
// from the sensor. The stream is a byte stream; clients reassemble by the 0x22 header.
#define FRAME_LEN         14
#define STREAM_PERIOD_MS  10       // ~100 Hz

APP_TIMER_DEF(m_stream_timer);

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;
static uint16_t m_rx_handle;
static uint16_t m_tx_handle;
static uint16_t m_vendor_handle;
static uint16_t m_bas_handle;
static bool m_streaming;
static bool m_active;

static void add_notify_char(uint16_t svc, uint8_t type, uint16_t uuid16, uint16_t max_len,
                            bool write, bool wwr, bool read, ble_gatts_char_handles_t * out)
{
    ble_gatts_char_md_t char_md = { 0 };
    ble_gatts_attr_md_t cccd_md = { 0 };
    ble_gatts_attr_md_t attr_md = { 0 };
    ble_gatts_attr_t attr = { 0 };
    ble_uuid_t uuid = { .uuid = uuid16, .type = type };

    // TX and the battery level notify, so give them a CCCD
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
    cccd_md.vloc = BLE_GATTS_VLOC_STACK;

    char_md.char_props.notify = !write && !read ? 1 : 0;  // TX: notify only
    char_md.char_props.write = write;
    char_md.char_props.write_wo_resp = wwr;
    char_md.char_props.read = read;
    if (char_md.char_props.notify)
        char_md.p_cccd_md = &cccd_md;

    if (write || wwr)
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    else
        BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
    if (read)
        BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    else
        BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.read_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr_md.vlen = 1;

    attr.p_uuid = &uuid;
    attr.p_attr_md = &attr_md;
    attr.init_len = 0;
    attr.max_len = max_len;

    APP_ERROR_CHECK(sd_ble_gatts_characteristic_add(svc, &char_md, &attr, out));
}

static void add_nus(void)
{
    uint8_t type;
    uint16_t svc;
    ble_uuid_t svc_uuid;
    ble_gatts_char_handles_t h;

    APP_ERROR_CHECK(sd_ble_uuid_vs_add(&m_nus_base, &type));
    svc_uuid.type = type;
    svc_uuid.uuid = NUS_UUID_SVC;
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &svc_uuid, &svc));

    // RX: write / write-without-response, no read/notify
    add_notify_char(svc, type, NUS_UUID_RX, 20, true, true, false, &h);
    m_rx_handle = h.value_handle;
    // TX: notify only
    add_notify_char(svc, type, NUS_UUID_TX, 20, false, false, false, &h);
    m_tx_handle = h.value_handle;
    // Vendor: read/write, bit0 = LED (unused by games)
    add_notify_char(svc, type, NUS_UUID_VENDOR, 4, true, false, true, &h);
    m_vendor_handle = h.value_handle;
}

static void add_battery(void)
{
    uint16_t svc;
    ble_uuid_t svc_uuid;
    ble_gatts_char_handles_t h;

    BLE_UUID_BLE_ASSIGN(svc_uuid, BLE_UUID_BATTERY_SERVICE);
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &svc_uuid, &svc));
    add_notify_char(svc, BLE_UUID_TYPE_BLE, BLE_UUID_BATTERY_LEVEL_CHAR, 1, false, false, true, &h);
    m_bas_handle = h.value_handle;
}

static void add_dis(void)
{
    uint16_t svc;
    ble_uuid_t svc_uuid, chr_uuid;
    ble_gatts_char_md_t char_md = { 0 };
    ble_gatts_attr_md_t attr_md = { 0 };
    ble_gatts_attr_t attr = { 0 };
    ble_gatts_char_handles_t h;
    static const char fw_rev[] = "3.2.1-A";

    BLE_UUID_BLE_ASSIGN(svc_uuid, BLE_UUID_DEVICE_INFORMATION_SERVICE);
    APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &svc_uuid, &svc));

    BLE_UUID_BLE_ASSIGN(chr_uuid, BLE_UUID_FIRMWARE_REVISION_STRING_CHAR);
    char_md.char_props.read = 1;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&attr_md.write_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr.p_uuid = &chr_uuid;
    attr.p_attr_md = &attr_md;
    attr.init_len = sizeof(fw_rev) - 1;
    attr.max_len = sizeof(fw_rev) - 1;
    attr.p_value = (uint8_t *) fw_rev;
    APP_ERROR_CHECK(sd_ble_gatts_characteristic_add(svc, &char_md, &attr, &h));
}

static void update_battery_level(void)
{
    uint16_t mv = battery_mv();
    uint8_t pct = mv <= 2500 ? 0 : (mv >= 3000 ? 100 : (mv - 2500) / 5);
    ble_gatts_value_t val = { .len = 1, .offset = 0, .p_value = &pct };
    (void) sd_ble_gatts_value_set(m_conn_handle, m_bas_handle, &val);
}

static void stream_timer_handler(void * p_context)
{
    uint8_t frame[FRAME_LEN];
    uint16_t len = FRAME_LEN;
    ble_gatts_hvx_params_t hvx = { 0 };

    if (!m_streaming || m_conn_handle == BLE_CONN_HANDLE_INVALID)
        return;

    frame[0] = 0x22;
    // status bit0 = button state (active-low input, pressed = 0)
    frame[1] = (nrf_gpio_pin_read(BUTTON_1) == 0) ? 0x01 : 0x00;
    if (!imu_stream_read(&frame[2]))
        return;

    hvx.handle = m_tx_handle;
    hvx.type = BLE_GATT_HVX_NOTIFICATION;
    hvx.p_len = &len;
    hvx.p_data = frame;
    // Drop the frame if the link is momentarily busy; a game tolerates that
    (void) sd_ble_gatts_hvx(m_conn_handle, &hvx);
}

static void send_ready(void)
{
    uint8_t ready[5] = { 0x21, 0x00, 0x00, 0x00, 0x00 };
    uint16_t len = sizeof(ready);
    ble_gatts_hvx_params_t hvx = { .handle = m_tx_handle, .type = BLE_GATT_HVX_NOTIFICATION,
                                   .p_len = &len, .p_data = ready };
    (void) sd_ble_gatts_hvx(m_conn_handle, &hvx);
}

static void stream_start(void)
{
    if (m_streaming)
        return;
    if (!imu_stream_start())
        return;
    m_streaming = true;
    send_ready();
    APP_ERROR_CHECK(app_timer_start(m_stream_timer, APP_TIMER_TICKS(STREAM_PERIOD_MS), NULL));
}

void game_stop(void)
{
    if (!m_streaming)
        return;
    m_streaming = false;
    (void) app_timer_stop(m_stream_timer);
    imu_stream_stop(g_settings.motion_threshold_mg);
}

static void on_rx(const uint8_t * data, uint16_t len)
{
    // START = 20 10 ..., STOP = 20 00 ...; 0a/09 (score saving) need the original
    // device's secret key, which this firmware does not have, so they are ignored.
    if (!m_active)
        return;
    if (len >= 2 && data[0] == 0x20)
    {
        if (data[1] == 0x10)
            stream_start();
        else if (data[1] == 0x00)
            game_stop();
    }
}

void game_init(void)
{
    add_nus();
    add_battery();
    add_dis();
    APP_ERROR_CHECK(app_timer_create(&m_stream_timer, APP_TIMER_MODE_REPEATED, stream_timer_handler));
}

void game_on_ble_evt(ble_evt_t const * p_ble_evt, uint16_t conn_handle)
{
    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            m_conn_handle = conn_handle;
            m_streaming = false;
            update_battery_level();
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            game_stop();
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            break;

        case BLE_GATTS_EVT_WRITE:
        {
            ble_gatts_evt_write_t const * w = &p_ble_evt->evt.gatts_evt.params.write;
            if (w->handle == m_rx_handle)
                on_rx(w->data, w->len);
            else if (w->handle == m_vendor_handle && w->len >= 1)
                led_set(w->data[0] & 1);
            break;
        }

        default:
            break;
    }
}

bool game_streaming(void)
{
    return m_streaming;
}

void game_set_active(bool active)
{
    m_active = active;
    if (!active)
        game_stop();
}
