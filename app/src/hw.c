// Small hardware helpers for Zabka Triki: LED patterns, battery, SPI NOR sleep
#include "hw.h"

#include "boards.h"
#include "app_timer.h"
#include "app_error.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrfx_saadc.h"

APP_TIMER_DEF(m_led_timer);

static uint16_t m_led_steps;   // remaining on/off transitions
static uint16_t m_led_on_ms;
static uint16_t m_led_off_ms;
static bool     m_led_state;

void led_set(bool on)
{
    m_led_state = on;
    if (on == LEDS_ACTIVE_STATE)
        nrf_gpio_pin_set(LED_1);
    else
        nrf_gpio_pin_clear(LED_1);
}

static void led_timer_handler(void * p_context)
{
    if (m_led_steps == 0)
    {
        led_set(false);
        return;
    }
    m_led_steps--;
    led_set(!m_led_state);
    uint16_t next = m_led_state ? m_led_on_ms : m_led_off_ms;
    if (m_led_steps > 0)
        APP_ERROR_CHECK(app_timer_start(m_led_timer, APP_TIMER_TICKS(next), NULL));
    else
        led_set(false);
}

void led_init(void)
{
    nrf_gpio_cfg_output(LED_1);
    led_set(false);
    APP_ERROR_CHECK(app_timer_create(&m_led_timer, APP_TIMER_MODE_SINGLE_SHOT, led_timer_handler));
}

void led_blink(uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    if (count == 0)
        return;
    (void) app_timer_stop(m_led_timer);
    m_led_on_ms  = on_ms;
    m_led_off_ms = off_ms;
    // first "on" happens right now, then 2*count-1 transitions follow
    m_led_steps = (uint16_t)(2 * count - 1);
    led_set(true);
    APP_ERROR_CHECK(app_timer_start(m_led_timer, APP_TIMER_TICKS(on_ms), NULL));
}

void led_off(void)
{
    (void) app_timer_stop(m_led_timer);
    m_led_steps = 0;
    led_set(false);
}

static void saadc_handler(nrfx_saadc_evt_t const * p_event)
{
}

uint16_t battery_mv(void)
{
    nrf_saadc_value_t value = 0;
    nrfx_saadc_config_t config = NRFX_SAADC_DEFAULT_CONFIG;
    config.resolution = NRF_SAADC_RESOLUTION_12BIT;
    config.oversample = NRF_SAADC_OVERSAMPLE_8X;
    nrf_saadc_channel_config_t channel = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_VDD);
    channel.acq_time = NRF_SAADC_ACQTIME_10US;
    channel.burst = NRF_SAADC_BURST_ENABLED;

    if (nrfx_saadc_init(&config, saadc_handler) != NRFX_SUCCESS)
        return 0;
    if (nrfx_saadc_channel_init(0, &channel) == NRFX_SUCCESS)
        (void) nrfx_saadc_sample_convert(0, &value);
    nrfx_saadc_uninit();

    if (value < 0)
        value = 0;
    // 0.6 V reference, gain 1/6 -> full scale 3.6 V at 12 bits
    return (uint16_t)(((uint32_t) value * 3600) / 4096);
}

static void nor_send_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--)
    {
        if (b & (1 << i))
            nrf_gpio_pin_set(NOR_MOSI_PIN);
        else
            nrf_gpio_pin_clear(NOR_MOSI_PIN);
        nrf_delay_us(1);
        nrf_gpio_pin_set(NOR_SCK_PIN);
        nrf_delay_us(1);
        nrf_gpio_pin_clear(NOR_SCK_PIN);
    }
}

static void nor_command(uint8_t cmd)
{
    nrf_gpio_pin_clear(NOR_CS_PIN);
    nrf_delay_us(1);
    nor_send_byte(cmd);
    nrf_delay_us(1);
    nrf_gpio_pin_set(NOR_CS_PIN);
}

void nor_deep_power_down(void)
{
    // Pins keep their output state in sleep, so CS stays high afterwards
    nrf_gpio_pin_set(NOR_CS_PIN);
    nrf_gpio_cfg_output(NOR_CS_PIN);
    nrf_gpio_pin_clear(NOR_SCK_PIN);
    nrf_gpio_cfg_output(NOR_SCK_PIN);
    nrf_gpio_pin_clear(NOR_MOSI_PIN);
    nrf_gpio_cfg_output(NOR_MOSI_PIN);
    // MISO is driven by the flash only while CS is low; pull it down so it never floats
    nrf_gpio_cfg_input(NOR_MISO_PIN, NRF_GPIO_PIN_PULLDOWN);

    // Wake it first in case it is already in DPD, then send "deep power-down"
    nor_command(0xAB);
    nrf_delay_us(50);
    nor_command(0xB9);
    nrf_delay_us(20);
    nrf_gpio_cfg_default(NOR_MISO_PIN);
}
