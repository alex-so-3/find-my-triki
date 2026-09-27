// Persistent settings (FDS), layout independent of the GATT protocol
#include "settings.h"

#include <string.h>
#include <stddef.h>
#include "app_error.h"
#include "fds.h"
#include "nrf_sdh.h"
#include "nrf_soc.h"
#include "default_keys.h"

#define SETTINGS_FILE_ID   0x7115
#define SETTINGS_REC_KEY   0x0001
#define SETTINGS_MAGIC     0x54524B31   // "TRK1"

settings_t g_settings;

static volatile bool m_fds_ready;
static volatile bool m_fds_busy;
static bool m_gc_pending;

static const settings_t m_defaults =
{
    .magic               = SETTINGS_MAGIC,
    .apple_key           = DEFAULT_APPLE_KEY,
    .fmdn_key            = DEFAULT_FMDN_KEY,
    .auth                = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h' },
    .settings_mac        = { 0 },
    .apple_enabled       = DEFAULT_APPLE_ENABLED,
    .fmdn_enabled        = DEFAULT_FMDN_ENABLED,
    .period              = 2,
    .tx_power            = 2,
    .still_timeout_s     = 600,
    .motion_threshold_mg = 63,
    .status_flags        = 0x448000,
    .game_mac            = DEFAULT_GAME_MAC,
    .game_name_len       = DEFAULT_GAME_NAME_LEN,
    .game_name           = DEFAULT_GAME_NAME,
};

static void fds_evt_handler(fds_evt_t const * p_evt)
{
    switch (p_evt->id)
    {
        case FDS_EVT_INIT:
            if (p_evt->result == NRF_SUCCESS)
                m_fds_ready = true;
            break;
        case FDS_EVT_WRITE:
        case FDS_EVT_UPDATE:
        case FDS_EVT_GC:
            m_fds_busy = false;
            break;
        default:
            break;
    }
}

static void wait_for(volatile bool * flag, bool value)
{
    while (*flag != value)
        (void) sd_app_evt_wait();
}

void settings_init(void)
{
    fds_record_desc_t desc = { 0 };
    fds_find_token_t tok = { 0 };
    fds_flash_record_t rec;

    APP_ERROR_CHECK(fds_register(fds_evt_handler));
    APP_ERROR_CHECK(fds_init());
    wait_for(&m_fds_ready, true);

    g_settings = m_defaults;
    if (fds_record_find(SETTINGS_FILE_ID, SETTINGS_REC_KEY, &desc, &tok) == NRF_SUCCESS &&
        fds_record_open(&desc, &rec) == NRF_SUCCESS)
    {
        const settings_t * stored = rec.p_data;
        uint32_t stored_bytes = rec.p_header->length_words * 4;
        // Fields are only ever appended, so copying min(stored, current) preserves every
        // existing value and leaves anything newer at its default (zero).
        if (stored_bytes >= offsetof(settings_t, game_mac) && stored->magic == SETTINGS_MAGIC)
            memcpy(&g_settings, stored, stored_bytes < sizeof(settings_t) ? stored_bytes : sizeof(settings_t));
        (void) fds_record_close(&desc);
    }
}

void settings_save(void)
{
    static settings_t shadow;   // FDS needs the data to stay valid until the write completes
    fds_record_desc_t desc = { 0 };
    fds_find_token_t tok = { 0 };
    ret_code_t err;

    wait_for(&m_fds_busy, false);
    shadow = g_settings;
    fds_record_t rec =
    {
        .file_id = SETTINGS_FILE_ID,
        .key = SETTINGS_REC_KEY,
        .data = { .p_data = &shadow, .length_words = (sizeof(shadow) + 3) / 4 },
    };

    m_fds_busy = true;
    if (fds_record_find(SETTINGS_FILE_ID, SETTINGS_REC_KEY, &desc, &tok) == NRF_SUCCESS)
        err = fds_record_update(&desc, &rec);
    else
        err = fds_record_write(NULL, &rec);

    if (err == FDS_ERR_NO_SPACE_IN_FLASH && !m_gc_pending)
    {
        // Reclaim space from old record versions and retry once
        m_gc_pending = true;
        APP_ERROR_CHECK(fds_gc());
        wait_for(&m_fds_busy, false);
        m_gc_pending = false;
        settings_save();
        return;
    }
    if (err != NRF_SUCCESS)
        m_fds_busy = false;
}

bool settings_busy(void)
{
    return m_fds_busy;
}

static bool is_zero(const uint8_t * p, int len)
{
    for (int i = 0; i < len; i++)
        if (p[i])
            return false;
    return true;
}

bool settings_has_apple_key(void)
{
    return !is_zero(g_settings.apple_key, APPLE_KEY_LEN);
}

bool settings_has_fmdn_key(void)
{
    return !is_zero(g_settings.fmdn_key, FMDN_KEY_LEN);
}

bool settings_has_game(void)
{
    return !is_zero(g_settings.game_mac, 6);
}
