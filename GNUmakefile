# pico2w-wigle: WiGLE collector firmware for the Raspberry Pi Pico 2 W.
#
#   make config    Ask for the device settings and write GNUmakefile.local
#   make           Build $(BUILD_DIR)/pico2w-wigle.uf2
#   make install   Build and flash the firmware onto a Pico 2 W
#   make clean     Remove the build directory
#
# Settings live in GNUmakefile.local (git-ignored, created by `make config`
# or automatically on the first `make` / `make install`). Any variable below
# can also be overridden there or on the command line.

-include GNUmakefile.local

# Build environment.
PICO_SDK_VERSION ?= 2.3.1
PICO_SDK_PATH    ?= $(CURDIR)/.deps/pico-sdk
PICO_BOARD       ?= pico2_w
BUILD_DIR        ?= build
# Mount point of the RP2350 drive in BOOTSEL mode; auto-detected when empty.
PICO_MOUNT       ?=

# Device settings, normally written by `make config`.
WIFI_SSID        ?=
WIFI_PASSWORD    ?=
WIFI_COUNTRY     ?= XX
GPS_LAT          ?=
GPS_LON          ?=
GPS_ALT          ?= 0
GPS_ACC          ?= 10
WIGLE_API_NAME   ?=
WIGLE_API_TOKEN  ?=
# Proxy used by this machine (curl syntax), e.g. socks5h://127.0.0.1:1080.
LOCAL_PROXY      ?=
# SOCKS5 proxy used by the Pico, e.g. socks5://192.168.1.10:1080.
PICO_PROXY       ?=
NTP_SERVER       ?= pool.ntp.org
SCAN_INTERVAL    ?= 10
UPLOAD_INTERVAL  ?= 21600

# Scripts read the settings from the environment, which avoids any shell
# quoting of passwords.
export WIFI_SSID WIFI_PASSWORD WIFI_COUNTRY GPS_LAT GPS_LON GPS_ALT GPS_ACC \
	WIGLE_API_NAME WIGLE_API_TOKEN LOCAL_PROXY PICO_PROXY NTP_SERVER \
	SCAN_INTERVAL UPLOAD_INTERVAL

# curl and git (libcurl) both honor ALL_PROXY, including socks5h://.
PROXY_ENV = $(if $(LOCAL_PROXY),ALL_PROXY="$$LOCAL_PROXY")

UF2      := $(BUILD_DIR)/pico2w-wigle.uf2
CA_PEM   := $(BUILD_DIR)/le-roots.pem
CONFIG_H := $(BUILD_DIR)/generated/config.h
SDK_INIT := $(PICO_SDK_PATH)/pico_sdk_init.cmake

# Let's Encrypt root certificates, relative to https://letsencrypt.org/certs/.
LE_CERTS := isrgrootx1.pem isrg-root-x2.pem gen-y/root-yr.pem gen-y/root-ye.pem

.PHONY: all config install clean distclean

all: $(CA_PEM) $(BUILD_DIR)/CMakeCache.txt
	@scripts/gen-config.sh $(CONFIG_H) $(CA_PEM)
	@$(PROXY_ENV) cmake --build $(BUILD_DIR) --parallel

config:
	@scripts/config.sh GNUmakefile.local

# Ask for the settings on the first build; make then restarts and reads the
# new GNUmakefile.local.
ifneq ($(filter all install,$(or $(MAKECMDGOALS),all)),)
GNUmakefile.local:
	@scripts/config.sh $@
endif

install: all
	@mnt='$(PICO_MOUNT)'; \
	if [ -z "$$mnt" ]; then \
		for d in /media/$$USER/RP2350 /run/media/$$USER/RP2350 /Volumes/RP2350; do \
			if [ -d "$$d" ]; then mnt=$$d; break; fi; \
		done; \
	fi; \
	if [ -n "$$mnt" ]; then \
		echo "Copying $(UF2) to $$mnt"; \
		cp $(UF2) "$$mnt/" && sync; \
	elif command -v picotool >/dev/null 2>&1; then \
		picotool load -u -v -x -f $(UF2); \
	else \
		echo 'error: no RP2350 drive found. Hold BOOTSEL while plugging in the Pico 2 W, or set PICO_MOUNT.' >&2; \
		exit 1; \
	fi

$(CA_PEM):
	@mkdir -p $(@D)
	@echo "Downloading Let's Encrypt root certificates"
	@for c in $(LE_CERTS); do \
		$(PROXY_ENV) curl -fsSL --max-time 60 https://letsencrypt.org/certs/$$c || exit 1; \
	done > $@.tmp
	@if [ "$$(grep -c -- '-----END CERTIFICATE-----' $@.tmp)" != $(words $(LE_CERTS)) ]; then \
		echo 'error: unexpected content from letsencrypt.org' >&2; rm -f $@.tmp; exit 1; \
	fi
	@mv $@.tmp $@

$(BUILD_DIR)/CMakeCache.txt: | $(SDK_INIT)
	@$(PROXY_ENV) cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release \
		-DPICO_SDK_PATH=$(PICO_SDK_PATH) -DPICO_BOARD=$(PICO_BOARD)

$(SDK_INIT):
	$(PROXY_ENV) git clone --depth 1 --branch $(PICO_SDK_VERSION) \
		https://github.com/raspberrypi/pico-sdk.git $(PICO_SDK_PATH)
	$(PROXY_ENV) git -C $(PICO_SDK_PATH) submodule update --init --depth 1 \
		lib/cyw43-driver lib/lwip lib/mbedtls lib/tinyusb

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -rf .deps
