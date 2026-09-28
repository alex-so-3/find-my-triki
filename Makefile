# Triki tag: SoftDevice S112 + secure BLE bootloader + application (nRF52810)
#
#   make                  build everything -> build/triki_full.hex (SD + bootloader + app)
#   make dfu              signed OTA package -> build/triki_app_dfu.zip
#   make flash            erase chip and program build/triki_full.hex (OpenOCD, CMSIS-DAP)
#   make flash-app        program only the application (keeps settings and keys)
#   make recover          unlock an APPROTECT-locked chip (erases everything!)
#   PROBE=jlink make ...  use nrfjprog / J-Link instead of OpenOCD
#
#   Options: LFCLK=RC  DCDC=0  IMU_INT2=1  LED_ACTIVE_HIGH=1  DEBUG_CHR=1  (see app/armgcc/Makefile)
#   After changing options run `make clean` (flags are not tracked by the SDK makefiles)
#   APP_VERSION=n         application version for DFU (must not go down)

include toolchain.mk

APP_VERSION ?= 18
LFCLK ?= XTAL
DCDC ?= 1
DEBUG_CHR ?= 0
WDT ?= 1
IMU_INT2 ?= 0
LED_ACTIVE_HIGH ?= 0
BL_VERSION ?= 1
PROBE ?= openocd
OPENOCD ?= openocd
OPENOCD_IF ?= interface/cmsis-dap.cfg

BUILD := build
SD_HEX := $(SDK_ROOT)/components/softdevice/s112/hex/s112_nrf52_7.2.0_softdevice.hex
SD_FWID := 0x0103
BL_HEX := bootloader/armgcc/_build/nrf52810_xxaa_s112.hex
APP_HEX := app/armgcc/_build/triki_app.hex
SETTINGS_HEX := $(BUILD)/bl_settings.hex
FULL_HEX := $(BUILD)/triki_full.hex
DFU_ZIP := $(BUILD)/triki_app_dfu.zip
DFU_KEY := keys/dfu_private.pem
NRFUTIL := nrfutil nrf5sdk-tools

.PHONY: all app bootloader dfu flash flash-app recover keys clean FORCE

all: $(FULL_HEX)

keys: keys/dfu_public_key.c

# The private key signs OTA packages: keep it safe, without it no OTA is possible
$(DFU_KEY):
	@mkdir -p keys
	$(NRFUTIL) keys generate $@

keys/dfu_public_key.c: $(DFU_KEY)
	$(NRFUTIL) keys display --key pk --format code $< --out_file $@

bootloader: keys/dfu_public_key.c
	$(MAKE) -C bootloader/armgcc LED_ACTIVE_HIGH=$(LED_ACTIVE_HIGH)

app:
	$(MAKE) -C app/armgcc APP_VERSION=$(APP_VERSION) LFCLK=$(LFCLK) DCDC=$(DCDC) DEBUG_CHR=$(DEBUG_CHR) WDT=$(WDT) IMU_INT2=$(IMU_INT2) LED_ACTIVE_HIGH=$(LED_ACTIVE_HIGH)

$(BL_HEX): bootloader
$(APP_HEX): app

# Settings include the backup copy (MBR params page): the bootloader reverts
# settings that differ from the backup, which would invalidate a SWD-flashed app
$(SETTINGS_HEX): $(APP_HEX) FORCE
	@mkdir -p $(BUILD)
	$(NRFUTIL) settings generate --family NRF52810 --application $(APP_HEX) \
		--application-version $(APP_VERSION) --bootloader-version $(BL_VERSION) \
		--bl-settings-version 2 $@

$(FULL_HEX): $(BL_HEX) $(APP_HEX) $(SETTINGS_HEX)
	python3 tools/mergehex.py $@ $(SD_HEX) $(BL_HEX) $(APP_HEX) $(SETTINGS_HEX)
	@echo "Built $@"

dfu: $(APP_HEX) $(DFU_KEY)
	@mkdir -p $(BUILD)
	$(NRFUTIL) pkg generate --hw-version 52 --sd-req $(SD_FWID) \
		--application $(APP_HEX) --application-version $(APP_VERSION) \
		--key-file $(DFU_KEY) $(DFU_ZIP)
	@echo "Built $(DFU_ZIP) - upload with nRF Connect / nRF Device Firmware Update to TrikiTagDFU"

ifeq ($(PROBE),jlink)
flash: $(FULL_HEX)
	nrfjprog -f nrf52 --program $(FULL_HEX) --chiperase --verify
	nrfjprog -f nrf52 --reset

flash-app: $(APP_HEX) $(SETTINGS_HEX)
	nrfjprog -f nrf52 --program $(APP_HEX) --sectorerase --verify
	nrfjprog -f nrf52 --program $(SETTINGS_HEX) --sectorerase --verify
	nrfjprog -f nrf52 --reset

recover:
	nrfjprog -f nrf52 --recover
else
OPENOCD_CMD = $(OPENOCD) -f $(OPENOCD_IF) -c "transport select swd" -f target/nrf52.cfg

flash: $(FULL_HEX)
	$(OPENOCD_CMD) -c "init; halt; nrf5 mass_erase; program $(FULL_HEX) verify; reset run; exit"

flash-app: $(APP_HEX) $(SETTINGS_HEX)
	python3 tools/mergehex.py $(BUILD)/app_and_settings.hex $(APP_HEX) $(SETTINGS_HEX)
	$(OPENOCD_CMD) -c "init; halt; program $(BUILD)/app_and_settings.hex verify; reset run; exit"

recover:
	$(OPENOCD_CMD) -c "init; nrf52_recover; exit"
endif

clean:
	$(MAKE) -C app/armgcc clean
	$(MAKE) -C bootloader/armgcc clean
	rm -rf $(BUILD)
