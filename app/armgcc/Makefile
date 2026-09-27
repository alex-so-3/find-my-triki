PROJECT_NAME     := triki_app
TARGETS          := triki_app
OUTPUT_DIRECTORY := _build

ROOT_DIR := ../..
include $(ROOT_DIR)/toolchain.mk
PROJ_DIR := ..

# Build options (make LFCLK=RC DCDC=1 IMU_INT2=1)
#   LFCLK=RC  - use the internal RC oscillator if the 32.768 kHz crystal is missing
#   DCDC=1    - enable the DC/DC regulator (only if the inductor is populated!)
#   LED_ACTIVE_HIGH=1 - LED lights when P0.28 is high (Triki: active low)
#   IMU_INT2=1 - route the LSM6DSL wake-up event to INT2 (P0.10) instead of INT1 (P0.09)
LFCLK ?= XTAL
DCDC ?= 0
IMU_INT2 ?= 0
LED_ACTIVE_HIGH ?= 0
APP_VERSION ?= 0

$(OUTPUT_DIRECTORY)/triki_app.out: \
  LINKER_SCRIPT  := triki_app.ld

SRC_FILES += \
  $(SDK_ROOT)/modules/nrfx/mdk/gcc_startup_nrf52810.S \
  $(SDK_ROOT)/modules/nrfx/mdk/system_nrf52810.c \
  $(SDK_ROOT)/components/libraries/button/app_button.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_handler_gcc.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer2.c \
  $(SDK_ROOT)/components/libraries/timer/drv_rtc.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/atomic/nrf_atomic.c \
  $(SDK_ROOT)/components/libraries/atomic_fifo/nrf_atfifo.c \
  $(SDK_ROOT)/components/libraries/experimental_section_vars/nrf_section_iter.c \
  $(SDK_ROOT)/components/libraries/sortlist/nrf_sortlist.c \
  $(SDK_ROOT)/components/libraries/fds/fds.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage.c \
  $(SDK_ROOT)/components/libraries/fstorage/nrf_fstorage_sd.c \
  $(SDK_ROOT)/modules/nrfx/soc/nrfx_atomic.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_gpiote.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_twim.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_saadc.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/nrfx_wdt.c \
  $(SDK_ROOT)/modules/nrfx/drivers/src/prs/nrfx_prs.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_ble.c \
  $(SDK_ROOT)/components/softdevice/common/nrf_sdh_soc.c \
  $(PROJ_DIR)/src/main.c \
  $(PROJ_DIR)/src/adv.c \
  $(PROJ_DIR)/src/config_svc.c \
  $(PROJ_DIR)/src/hw.c \
  $(PROJ_DIR)/src/imu.c \
  $(PROJ_DIR)/src/settings.c \
  $(PROJ_DIR)/src/std_svc.c \
  $(PROJ_DIR)/src/game.c \

INC_FOLDERS += \
  $(PROJ_DIR)/src \
  $(PROJ_DIR)/config \
  $(ROOT_DIR)/board \
  $(ROOT_DIR)/keys \
  $(SDK_ROOT)/components \
  $(SDK_ROOT)/components/boards \
  $(SDK_ROOT)/components/softdevice/s112/headers \
  $(SDK_ROOT)/components/softdevice/s112/headers/nrf52 \
  $(SDK_ROOT)/components/softdevice/common \
  $(SDK_ROOT)/components/ble/common \
  $(SDK_ROOT)/components/libraries/util \
  $(SDK_ROOT)/components/libraries/atomic \
  $(SDK_ROOT)/components/libraries/atomic_fifo \
  $(SDK_ROOT)/components/libraries/button \
  $(SDK_ROOT)/components/libraries/timer \
  $(SDK_ROOT)/components/libraries/delay \
  $(SDK_ROOT)/components/libraries/experimental_section_vars \
  $(SDK_ROOT)/components/libraries/sortlist \
  $(SDK_ROOT)/components/libraries/fds \
  $(SDK_ROOT)/components/libraries/fstorage \
  $(SDK_ROOT)/components/libraries/log \
  $(SDK_ROOT)/components/libraries/log/src \
  $(SDK_ROOT)/components/libraries/strerror \
  $(SDK_ROOT)/components/libraries/mutex \
  $(SDK_ROOT)/components/toolchain/cmsis/include \
  $(SDK_ROOT)/modules/nrfx \
  $(SDK_ROOT)/modules/nrfx/hal \
  $(SDK_ROOT)/modules/nrfx/mdk \
  $(SDK_ROOT)/modules/nrfx/drivers/include \
  $(SDK_ROOT)/integration/nrfx \
  $(SDK_ROOT)/integration/nrfx/legacy \

OPT = -Os -g3 -flto

COMMON_FLAGS += -DAPP_TIMER_V2
COMMON_FLAGS += -DAPP_TIMER_V2_RTC1_ENABLED
COMMON_FLAGS += -DBOARD_CUSTOM -DCUSTOM_BOARD_INC=triki_board
COMMON_FLAGS += -DCONFIG_GPIO_AS_PINRESET
COMMON_FLAGS += -DFLOAT_ABI_SOFT
COMMON_FLAGS += -DNRF52810_XXAA
COMMON_FLAGS += -DNRF52_PAN_74
COMMON_FLAGS += -DNRF_SD_BLE_API_VERSION=7
COMMON_FLAGS += -DS112
COMMON_FLAGS += -DSOFTDEVICE_PRESENT
COMMON_FLAGS += -DHAS_DCDC=$(DCDC)
COMMON_FLAGS += -DIMU_USE_INT2=$(IMU_INT2)
COMMON_FLAGS += -DAPP_VERSION=$(APP_VERSION)
ifeq ($(LED_ACTIVE_HIGH), 1)
COMMON_FLAGS += -DLEDS_ACTIVE_STATE=1
endif
ifeq ($(LFCLK), RC)
COMMON_FLAGS += -DNRF_SDH_CLOCK_LF_SRC=0 -DNRF_SDH_CLOCK_LF_RC_CTIV=16
COMMON_FLAGS += -DNRF_SDH_CLOCK_LF_RC_TEMP_CTIV=2 -DNRF_SDH_CLOCK_LF_ACCURACY=1
endif

CFLAGS += $(OPT) $(COMMON_FLAGS)
CFLAGS += -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=soft
CFLAGS += -Wall -Werror
# GCC 13 false positives on SDK section-variable iterators
CFLAGS += -Wno-array-bounds -Wno-stringop-overflow -Wno-stringop-overread
CFLAGS += -ffunction-sections -fdata-sections -fno-strict-aliasing
CFLAGS += -fno-builtin -fshort-enums

ASMFLAGS += -g3 $(COMMON_FLAGS)
ASMFLAGS += -mcpu=cortex-m4 -mthumb -mabi=aapcs -mfloat-abi=soft

LDFLAGS += $(OPT)
LDFLAGS += -mthumb -mabi=aapcs -L$(SDK_ROOT)/modules/nrfx/mdk -T$(LINKER_SCRIPT)
LDFLAGS += -mcpu=cortex-m4
LDFLAGS += -Wl,--gc-sections
LDFLAGS += --specs=nano.specs

triki_app: CFLAGS += -D__HEAP_SIZE=0 -D__STACK_SIZE=2048
triki_app: ASMFLAGS += -D__HEAP_SIZE=0 -D__STACK_SIZE=2048

LIB_FILES += -lc -lnosys -lm

.PHONY: default
default: triki_app

TEMPLATE_PATH := $(SDK_ROOT)/components/toolchain/gcc
include $(TEMPLATE_PATH)/Makefile.common
$(foreach target, $(TARGETS), $(call define_target, $(target)))
