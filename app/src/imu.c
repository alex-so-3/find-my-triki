// LSM6DSL used only as a low-power wake-on-motion source
#include "imu.h"

#include "boards.h"
#include "app_error.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrfx_twim.h"
#include "nrfx_gpiote.h"

// The wake-up event can be routed to INT1 (P0.09, uncertain) or INT2 (P0.10).
// Build with IMU_USE_INT2=1 if the multimeter says INT1 is not wired.
#if defined(IMU_USE_INT2) && IMU_USE_INT2
#define IMU_INT_PIN        IMU_INT2_PIN
#define IMU_REG_MD_CFG     0x5F    // MD2_CFG
#else
#define IMU_INT_PIN        IMU_INT1_PIN
#define IMU_REG_MD_CFG     0x5E    // MD1_CFG
#endif

#define IMU_ID             0x6A
#define REG_WHO_AM_I       0x0F
#define REG_CTRL1_XL       0x10
#define REG_CTRL2_G        0x11
#define REG_CTRL3_C        0x12
#define REG_CTRL6_C        0x15
#define REG_CTRL7_G        0x16
#define REG_WAKE_UP_SRC    0x1B
#define REG_TAP_CFG        0x58
#define REG_WAKE_UP_THS    0x5B
#define REG_WAKE_UP_DUR    0x5C
#define REG_OUTX_L_G       0x22    // gyro X..Z then accel X..Z, 12 bytes

#define CTRL1_XL_104HZ_16G 0x44    // ODR 104 Hz, +-16 g
#define CTRL2_G_104HZ_2000 0x4C    // ODR 104 Hz, +-2000 dps

#define CTRL1_XL_1_6HZ     0xB0    // ODR 1.6 Hz (low-power mode only), +-2 g
#define CTRL3_C_BDU_INC    0x44
#define CTRL3_C_SW_RESET   0x01
#define CTRL6_C_XL_LP      0x10    // XL_HM_MODE = 1 -> low power
#define CTRL7_G_LP         0x80
#define TAP_CFG_INT_LIR    0x81    // INTERRUPTS_ENABLE | LIR (latched)
#define MD_CFG_INT_WU      0x20

static const nrfx_twim_t m_twim = NRFX_TWIM_INSTANCE(0);
static imu_motion_handler_t m_handler;
static bool m_present;

static ret_code_t imu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    nrfx_twim_xfer_desc_t xfer = NRFX_TWIM_XFER_DESC_TX(IMU_I2C_ADDR, buf, sizeof(buf));
    return nrfx_twim_xfer(&m_twim, &xfer, 0);
}

static ret_code_t imu_read(uint8_t reg, uint8_t * val)
{
    nrfx_twim_xfer_desc_t xfer = NRFX_TWIM_XFER_DESC_TXRX(IMU_I2C_ADDR, &reg, 1, val, 1);
    return nrfx_twim_xfer(&m_twim, &xfer, 0);
}

static void imu_int_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    // Throttle: stay quiet until the application re-arms us
    nrfx_gpiote_in_event_disable(IMU_INT_PIN);
    if (m_handler)
        m_handler();
}

static uint8_t threshold_to_reg(uint16_t threshold_mg)
{
    // 1 LSB = FS/64 = 31.25 mg at +-2 g
    uint32_t ths = (threshold_mg + 15) / 31;
    if (ths < 1)
        ths = 1;
    if (ths > 0x3F)
        ths = 0x3F;
    return (uint8_t) ths;
}

// Program the sensor for latched wake-on-motion at 1.6 Hz low power (TWIM must be enabled).
// The slowest rate the accelerometer offers: motion is noticed within ~0.6 s, which is plenty
// for a tracker, and the sensor - the largest continuous draw on the board - idles longest.
static ret_code_t configure_wakeup(uint16_t threshold_mg)
{
    ret_code_t err;
    err  = imu_write(REG_CTRL3_C, CTRL3_C_SW_RESET);
    nrf_delay_ms(5);
    err |= imu_write(REG_CTRL3_C, CTRL3_C_BDU_INC);
    err |= imu_write(REG_CTRL2_G, 0);               // gyro power-down
    err |= imu_write(REG_CTRL7_G, CTRL7_G_LP);
    err |= imu_write(REG_CTRL6_C, CTRL6_C_XL_LP);   // accel low-power mode
    err |= imu_write(REG_CTRL1_XL, CTRL1_XL_1_6HZ);
    err |= imu_write(REG_WAKE_UP_DUR, 0);
    err |= imu_write(REG_WAKE_UP_THS, threshold_to_reg(threshold_mg));
    err |= imu_write(REG_TAP_CFG, TAP_CFG_INT_LIR);
    err |= imu_write(IMU_REG_MD_CFG, MD_CFG_INT_WU);
    return err;
}

bool imu_init(uint16_t threshold_mg, imu_motion_handler_t handler)
{
    uint8_t id = 0;
    ret_code_t err;

    // CS high selects I2C mode (the output latch keeps it in sleep at no cost). SA0 is strapped
    // low on the board (address 0x6A); P0.04 is left alone, see board/triki_board.h.
    nrf_gpio_pin_set(IMU_CS_PIN);
    nrf_gpio_cfg_output(IMU_CS_PIN);
    nrf_delay_ms(20);

    nrfx_twim_config_t config = NRFX_TWIM_DEFAULT_CONFIG;
    config.scl = IMU_SCL_PIN;
    config.sda = IMU_SDA_PIN;
    config.frequency = NRF_TWIM_FREQ_400K;
    // Blocking mode (no handler): transfers are few and short
    err = nrfx_twim_init(&m_twim, &config, NULL, NULL);
    if (err != NRFX_SUCCESS)
        return false;
    nrfx_twim_enable(&m_twim);

    err = imu_read(REG_WHO_AM_I, &id);
    if (err != NRFX_SUCCESS || id != IMU_ID)
    {
        nrfx_twim_disable(&m_twim);
        m_present = false;
        return false;
    }

    if (threshold_mg == 0)
    {
        // Motion detection disabled: accel and gyro power-down (~3 uA)
        (void) imu_write(REG_CTRL1_XL, 0);
        (void) imu_write(REG_CTRL2_G, 0);
        nrfx_twim_disable(&m_twim);
        m_present = false;
        return false;
    }

    err = configure_wakeup(threshold_mg);
    nrfx_twim_disable(&m_twim);
    if (err != NRFX_SUCCESS)
        return false;

    m_handler = handler;
    if (!nrfx_gpiote_is_init())
        APP_ERROR_CHECK(nrfx_gpiote_init());
    // Low accuracy = PORT/SENSE event, no GPIOTE channel, no extra current
    nrfx_gpiote_in_config_t in_config = NRFX_GPIOTE_CONFIG_IN_SENSE_LOTOHI(false);
    in_config.pull = NRF_GPIO_PIN_NOPULL;
    APP_ERROR_CHECK(nrfx_gpiote_in_init(IMU_INT_PIN, &in_config, imu_int_handler));

    m_present = true;
    imu_rearm();
    return true;
}

void imu_rearm(void)
{
    uint8_t src;

    if (!m_present)
        return;
    nrfx_twim_enable(&m_twim);
    (void) imu_read(REG_WAKE_UP_SRC, &src);   // reading clears the latch
    nrfx_twim_disable(&m_twim);
    nrfx_gpiote_in_event_enable(IMU_INT_PIN, true);
}

#if defined(DEBUG_CHR) && DEBUG_CHR
bool imu_debug_i2c(bool write, uint8_t reg, uint8_t val, uint8_t * out)
{
    ret_code_t err;
    nrfx_twim_enable(&m_twim);
    err = write ? imu_write(reg, val) : imu_read(reg, out);
    nrfx_twim_disable(&m_twim);
    return err == NRFX_SUCCESS;
}
#endif

bool imu_present(void)
{
    return m_present;
}


bool imu_stream_start(void)
{
    if (!m_present)
        return false;
    // Keep the wake-up interrupt from firing while we drive the sensor fast
    nrfx_gpiote_in_event_disable(IMU_INT_PIN);
    nrfx_twim_enable(&m_twim);
    (void) imu_write(IMU_REG_MD_CFG, 0);            // no motion interrupt in this mode
    (void) imu_write(REG_CTRL2_G, CTRL2_G_104HZ_2000);
    (void) imu_write(REG_CTRL1_XL, CTRL1_XL_104HZ_16G);
    // TWIM stays enabled for the whole streaming session
    return true;
}

bool imu_stream_read(uint8_t out[12])
{
    if (!m_present)
        return false;
    uint8_t reg = REG_OUTX_L_G;
    nrfx_twim_xfer_desc_t xfer = NRFX_TWIM_XFER_DESC_TXRX(IMU_I2C_ADDR, &reg, 1, out, 12);
    return nrfx_twim_xfer(&m_twim, &xfer, 0) == NRFX_SUCCESS;
}

void imu_stream_stop(uint16_t threshold_mg)
{
    if (!m_present)
        return;
    (void) configure_wakeup(threshold_mg);
    nrfx_twim_disable(&m_twim);
    imu_rearm();
}
