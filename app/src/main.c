// Triki tag: OpenHaystack + Google FMDN beacon for the Zabka Triki controller
// (nRF52810 + S112). The LSM6DSL wakes the tag on motion; without motion it
// advertises rarely. Button: short = battery, 3 s = config mode, 10 s = DFU.
#include <stdint.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf.h"
#include "boards.h"
#include "app_error.h"
#include "app_timer.h"
#include "app_button.h"
#include "ble.h"
#include "ble_hci.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_delay.h"
#include "nrfx_wdt.h"

#include "adv.h"
#include "config_svc.h"
#include "game.h"
#include "hw.h"
#include "imu.h"
#include "settings.h"
#include "std_svc.h"
#include "triki_approtect.h"

#define APP_BLE_CONN_CFG_TAG     1
#define APP_BLE_OBSERVER_PRIO    3

#define BOOTLOADER_DFU_START     0xB1

#define CONFIG_MODE_TIMEOUT_S    120
#define CONFIG_MAX_CONNECTION_S  300
// Connections without the correct password are dropped after this time
#define AUTH_TIMEOUT_S           20
#define MOTION_REARM_MS          30000
#define BATTERY_CHECK_MS         (3600UL * 1000)
#define BUTTON_CONFIG_S          3
#define BUTTON_DFU_S             10
#define WDT_TIMEOUT_MS           120000
// Two short clicks within this window = enter game-controller mode
#define DOUBLE_CLICK_MS          400
// Game advertising gives up and returns to the tracker after this without a connection
#define GAME_TIMEOUT_S           180
// After a disconnect, stay in game mode this long so a game can reconnect quickly
#define GAME_RECONNECT_S         120

// Per network one event every 2 ticks: moving 500 ms * period, still 5 s * period
#define TICK_MOVING_MS(p)        (500UL * (p))
#define TICK_STILL_MS(p)         (5000UL * (p))

APP_TIMER_DEF(m_beacon_timer);
APP_TIMER_DEF(m_still_timer);
APP_TIMER_DEF(m_rearm_timer);
APP_TIMER_DEF(m_button_timer);
APP_TIMER_DEF(m_session_timer);
APP_TIMER_DEF(m_click_timer);

static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;
static volatile bool m_config_mode;
static volatile bool m_game_mode;
static volatile bool m_pending_click;
static volatile bool m_save_and_reset;
static volatile bool m_moving = true;
static uint32_t m_tick_ms;
static uint32_t m_since_battery_ms;
static uint16_t m_battery_mv;
static bool m_imu_ok;
static uint8_t m_button_held_s;
static nrfx_wdt_channel_id m_wdt_channel;

void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    app_error_handler(0xDEADBEEF, line_num, p_file_name);
}

// Release build: any fatal error resets the chip
void app_error_fault_handler(uint32_t id, uint32_t pc, uint32_t info)
{
    NVIC_SystemReset();
}

static void enter_bootloader(void)
{
    adv_stop();
    APP_ERROR_CHECK(sd_power_gpregret_clr(0, 0xffffffff));
    APP_ERROR_CHECK(sd_power_gpregret_set(0, BOOTLOADER_DFU_START));
    (void) sd_nvic_SystemReset();
}

static void set_tick(uint32_t ms)
{
    if (ms == m_tick_ms)
        return;
    m_tick_ms = ms;
    (void) app_timer_stop(m_beacon_timer);
    APP_ERROR_CHECK(app_timer_start(m_beacon_timer, APP_TIMER_TICKS(ms), NULL));
}

static void update_runtime(void)
{
    config_svc_runtime_t rt = { .battery_mv = m_battery_mv, .moving = m_moving, .imu_ok = m_imu_ok };
    config_svc_set_runtime(&rt);
}

static void update_battery(void)
{
    m_battery_mv = battery_mv();
    adv_update_status(m_battery_mv);
    m_since_battery_ms = 0;
    update_runtime();
}

static void beacon_timer_handler(void * p_context)
{
    nrfx_wdt_channel_feed(m_wdt_channel);

    m_since_battery_ms += m_tick_ms;
    if (m_since_battery_ms >= BATTERY_CHECK_MS && !m_config_mode)
        update_battery();

    if (m_config_mode)
    {
        // Short heartbeat blink while waiting for a connection
        if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
            led_blink(1, 20, 0);
        return;
    }
    // No beacon advertising while connected or streaming as a game controller
    if (m_conn_handle == BLE_CONN_HANDLE_INVALID && !m_game_mode)
        adv_beacon_tick();
}

static void apply_motion_state(void)
{
    uint8_t period = g_settings.period ? g_settings.period : 2;

    if (m_config_mode)
        set_tick(1000);
    else
        set_tick(m_moving ? TICK_MOVING_MS(period) : TICK_STILL_MS(period));
}

static void still_timer_handler(void * p_context)
{
    m_moving = false;
    update_runtime();
    apply_motion_state();
}

static void rearm_timer_handler(void * p_context)
{
    imu_rearm();
}

// Runs in GPIOTE interrupt context
static void on_motion(void)
{
    bool was_moving = m_moving;

    m_moving = true;
    (void) app_timer_stop(m_still_timer);
    APP_ERROR_CHECK(app_timer_start(m_still_timer, APP_TIMER_TICKS(g_settings.still_timeout_s * 1000UL), NULL));
    APP_ERROR_CHECK(app_timer_start(m_rearm_timer, APP_TIMER_TICKS(MOTION_REARM_MS), NULL));
    if (!was_moving)
    {
        update_runtime();
        apply_motion_state();
        // Tell the networks right away that we are on the move
        if (!m_config_mode && m_conn_handle == BLE_CONN_HANDLE_INVALID)
            adv_beacon_tick();
    }
}

static void config_mode_enter(void)
{
    m_config_mode = true;
    apply_motion_state();
    adv_config_start(CONFIG_MODE_TIMEOUT_S);
    led_blink(3, 50, 50);
}

static void config_mode_exit(void)
{
    m_config_mode = false;
    (void) app_timer_stop(m_session_timer);
    adv_stop();
    apply_motion_state();
}

static void game_mode_enter(void)
{
    if (!settings_has_game())
    {
        // No identity configured (see settings / triki_config.py): nothing to advertise as
        led_blink(3, 40, 60);
        return;
    }
    m_game_mode = true;
    game_set_active(true);
    // The tracker's motion timers must not touch the sensor while it streams
    (void) app_timer_stop(m_still_timer);
    (void) app_timer_stop(m_rearm_timer);
    adv_game_start(g_settings.game_mac, g_settings.game_name, g_settings.game_name_len, GAME_TIMEOUT_S);
    led_blink(2, 60, 60);
}

static void game_mode_exit(void)
{
    m_game_mode = false;
    game_set_active(false);   // stop streaming and restore the low-power wake-up configuration
    imu_rearm();
    adv_stop();
    m_moving = true;
    update_runtime();
    if (m_imu_ok)
        APP_ERROR_CHECK(app_timer_start(m_still_timer, APP_TIMER_TICKS(g_settings.still_timeout_s * 1000UL), NULL));
    apply_motion_state();
}

static void session_timer_handler(void * p_context)
{
    if (m_conn_handle != BLE_CONN_HANDLE_INVALID)
        (void) sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
}

static void battery_blink(void)
{
    // 3 blinks = full, 2 = normal, 1 = low/critical
    uint8_t n = m_battery_mv > 2900 ? 3 : (m_battery_mv > 2700 ? 2 : 1);
    led_blink(n, 80, 250);
}

static void single_click_action(void)
{
    if (m_config_mode && m_conn_handle == BLE_CONN_HANDLE_INVALID)
        config_mode_exit();
    else
        battery_blink();
}

static void click_timer_handler(void * p_context)
{
    // The second click never came: treat it as a single click
    m_pending_click = false;
    single_click_action();
}

static void on_double_click(void)
{
    // Only from an idle tracker (not while configuring, connected or already gaming)
    if (m_config_mode || m_game_mode || m_conn_handle != BLE_CONN_HANDLE_INVALID)
        return;
    game_mode_enter();
}

static void button_timer_handler(void * p_context)
{
    m_button_held_s++;
    if (m_button_held_s == BUTTON_CONFIG_S)
        led_blink(1, 100, 0);
    else if (m_button_held_s == BUTTON_DFU_S)
        led_blink(5, 40, 60);
}

static void button_handler(uint8_t pin_no, uint8_t action)
{
    // In game mode the button belongs to the game (reported in the IMU frame), so the
    // battery/config/DFU click logic is suppressed
    if (m_game_mode)
        return;

    if (action == APP_BUTTON_PUSH)
    {
        m_button_held_s = 0;
        APP_ERROR_CHECK(app_timer_start(m_button_timer, APP_TIMER_TICKS(1000), NULL));
        return;
    }

    (void) app_timer_stop(m_button_timer);
    if (m_button_held_s >= BUTTON_DFU_S)
    {
        m_pending_click = false;
        enter_bootloader();
        return;
    }
    if (m_button_held_s >= BUTTON_CONFIG_S)
    {
        m_pending_click = false;
        if (m_conn_handle == BLE_CONN_HANDLE_INVALID)
            config_mode_enter();
        return;
    }
    // Short press: a second one within the window makes it a double click
    if (m_pending_click)
    {
        m_pending_click = false;
        (void) app_timer_stop(m_click_timer);
        on_double_click();
    }
    else
    {
        m_pending_click = true;
        APP_ERROR_CHECK(app_timer_start(m_click_timer, APP_TIMER_TICKS(DOUBLE_CLICK_MS), NULL));
    }
}

// DULT "play sound": the tag has no buzzer, so it blinks for 10 s
static void on_sound(bool start)
{
    if (start)
        led_blink(50, 100, 100);
    else
        led_off();
}

static void on_disconnected(void)
{
    m_conn_handle = BLE_CONN_HANDLE_INVALID;
    (void) app_timer_stop(m_session_timer);

    if (m_game_mode)
    {
        // game_on_ble_evt already stopped the stream and restored the sensor; keep advertising
        // as a controller for a short while so a brief drop can reconnect without leaving game mode
        adv_game_start(g_settings.game_mac, g_settings.game_name, g_settings.game_name_len, GAME_RECONNECT_S);
        return;
    }
    if (config_svc_dfu_requested() || std_svc_dfu_requested())
        enter_bootloader();
    if (config_svc_changed())
    {
        // FDS completes through SoftDevice events, so it cannot be waited for
        // here (we are inside the SoftDevice event handler): defer to main loop
        m_save_and_reset = true;
        return;
    }
    config_mode_exit();
}

static void session_timer_restart(uint32_t seconds)
{
    (void) app_timer_stop(m_session_timer);
    APP_ERROR_CHECK(app_timer_start(m_session_timer, APP_TIMER_TICKS(seconds * 1000UL), NULL));
}

static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    bool was_authorized = config_svc_authorized();

    if (p_ble_evt->header.evt_id == BLE_GAP_EVT_CONNECTED)
        m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;

    config_svc_on_ble_evt(p_ble_evt);
    std_svc_on_ble_evt(p_ble_evt);
    game_on_ble_evt(p_ble_evt, m_conn_handle);

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GAP_EVT_CONNECTED:
            // Game clients talk without a password; the config/beacon path drops
            // unauthorized strangers after a short window
            if (m_game_mode)
            {
                (void) app_timer_stop(m_still_timer);
                (void) app_timer_stop(m_rearm_timer);
            }
            else
                session_timer_restart(AUTH_TIMEOUT_S);
            break;

        case BLE_GATTS_EVT_WRITE:
            if (!was_authorized && config_svc_authorized())
            {
                led_blink(1, 300, 0);
                session_timer_restart(CONFIG_MAX_CONNECTION_S);
            }
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnected();
            break;

        case BLE_GAP_EVT_ADV_SET_TERMINATED:
            // Config/game advertising timed out without a connection
            if (m_conn_handle == BLE_CONN_HANDLE_INVALID &&
                p_ble_evt->evt.gap_evt.params.adv_set_terminated.reason ==
                    BLE_GAP_EVT_ADV_SET_TERMINATED_REASON_TIMEOUT)
            {
                if (m_config_mode)
                    config_mode_exit();
                else if (m_game_mode)
                    game_mode_exit();
            }
            break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            APP_ERROR_CHECK(sd_ble_gap_sec_params_reply(m_conn_handle,
                                                        BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL));
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
        {
            ble_gap_phys_t const phys = { .rx_phys = BLE_GAP_PHY_AUTO, .tx_phys = BLE_GAP_PHY_AUTO };
            APP_ERROR_CHECK(sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys));
            break;
        }

        case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:
            APP_ERROR_CHECK(sd_ble_gatts_exchange_mtu_reply(p_ble_evt->evt.gatts_evt.conn_handle,
                                                            BLE_GATT_ATT_MTU_DEFAULT));
            break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            APP_ERROR_CHECK(sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0));
            break;

        case BLE_GATTC_EVT_TIMEOUT:
            (void) sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            (void) sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                         BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            break;

        default:
            break;
    }
}

NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);

static void ble_stack_init(void)
{
    uint32_t ram_start = 0;

    APP_ERROR_CHECK(nrf_sdh_enable_request());
    APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start));
    APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));

    ble_gap_conn_params_t conn_params =
    {
        .min_conn_interval = MSEC_TO_UNITS(30, UNIT_1_25_MS),
        .max_conn_interval = MSEC_TO_UNITS(75, UNIT_1_25_MS),
        .slave_latency = 0,
        .conn_sup_timeout = MSEC_TO_UNITS(4000, UNIT_10_MS),
    };
    APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&conn_params));

    ble_gap_conn_sec_mode_t sec_mode;
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
    APP_ERROR_CHECK(sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *) "TrikiTag", 8));

#if defined(HAS_DCDC) && HAS_DCDC
    APP_ERROR_CHECK(sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE));
#endif
}

static void wdt_event_handler(void)
{
}

static void wdt_init(void)
{
    nrfx_wdt_config_t config = NRFX_WDT_DEAFULT_CONFIG;
    config.behaviour = NRF_WDT_BEHAVIOUR_RUN_SLEEP;
    config.reload_value = WDT_TIMEOUT_MS;
    APP_ERROR_CHECK(nrfx_wdt_init(&config, wdt_event_handler));
    APP_ERROR_CHECK(nrfx_wdt_channel_alloc(&m_wdt_channel));
    nrfx_wdt_enable();
}

static void timers_init(void)
{
    APP_ERROR_CHECK(app_timer_init());
    APP_ERROR_CHECK(app_timer_create(&m_beacon_timer, APP_TIMER_MODE_REPEATED, beacon_timer_handler));
    APP_ERROR_CHECK(app_timer_create(&m_still_timer, APP_TIMER_MODE_SINGLE_SHOT, still_timer_handler));
    APP_ERROR_CHECK(app_timer_create(&m_rearm_timer, APP_TIMER_MODE_SINGLE_SHOT, rearm_timer_handler));
    APP_ERROR_CHECK(app_timer_create(&m_button_timer, APP_TIMER_MODE_REPEATED, button_timer_handler));
    APP_ERROR_CHECK(app_timer_create(&m_session_timer, APP_TIMER_MODE_SINGLE_SHOT, session_timer_handler));
    APP_ERROR_CHECK(app_timer_create(&m_click_timer, APP_TIMER_MODE_SINGLE_SHOT, click_timer_handler));
}

static void buttons_init(void)
{
    static const app_button_cfg_t buttons[] =
    {
        { BUTTON_1, APP_BUTTON_ACTIVE_LOW, BUTTON_PULL, button_handler },
    };
    APP_ERROR_CHECK(app_button_init(buttons, ARRAY_SIZE(buttons), APP_TIMER_TICKS(50)));
    APP_ERROR_CHECK(app_button_enable());
}

int main(void)
{
    triki_keep_debug_open();

    // The SPI NOR is not used by this firmware: put it into deep power-down first
    nor_deep_power_down();

    timers_init();
    led_init();
    ble_stack_init();
    settings_init();
    config_svc_init();
    std_svc_init(on_sound);
    game_init();
    adv_init();
    update_battery();
    buttons_init();
    wdt_init();

    // Threshold 0 = IMU disabled (put into power-down), tag always "moving"
    bool imu_ok = imu_init(g_settings.motion_threshold_mg, on_motion);
    m_imu_ok = imu_ok;
    // Start as "moving" so the tag is visible right after power-up
    m_moving = true;
    update_runtime();
    if (imu_ok)
        APP_ERROR_CHECK(app_timer_start(m_still_timer, APP_TIMER_TICKS(g_settings.still_timeout_s * 1000UL), NULL));

    // Boot pattern: 1 long blink; extra 5 fast blinks = no keys yet, 2 slow = IMU missing
    if (!adv_anything_to_send())
        led_blink(5, 40, 120);
    else if (g_settings.motion_threshold_mg > 0 && !imu_ok)
        led_blink(2, 300, 300);
    else
        led_blink(1, 300, 0);

    apply_motion_state();

    for (;;)
    {
        (void) sd_app_evt_wait();
        if (m_save_and_reset)
        {
            m_save_and_reset = false;
            adv_stop();
            settings_save();
            while (settings_busy())
                (void) sd_app_evt_wait();
            led_blink(3, 30, 60);
            nrf_delay_ms(400);
            // Simplest way to apply everything (keys, IMU threshold, intervals)
            (void) sd_nvic_SystemReset();
        }
    }
}
