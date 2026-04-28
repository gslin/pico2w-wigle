## pico2w-wigle build, init, and install targets.
##
## This is intentionally a thin wrapper around the underlying CMake +
## Pico SDK build. The init target generates src/secrets.h from values
## that the user supplies interactively (and verifies the Wigle token
## with the API before saving anything).

.DEFAULT_GOAL := help

# ---------------------------------------------------------------------------
# Configuration knobs (override from the command line, e.g. `make build
# PICO_SDK_PATH=/opt/pico-sdk`).
# ---------------------------------------------------------------------------

PICO_SDK_PATH ?= $(HOME)/pico-sdk
PICO_BOARD    ?= pico2_w
BUILD_DIR     ?= build
BIN_NAME      ?= pico2w-wigle
JOBS          ?= $(shell nproc 2>/dev/null || echo 4)

UF2            := $(BUILD_DIR)/$(BIN_NAME).uf2
SECRETS_HEADER := src/secrets.h
CA_HEADER      := src/wigle_ca_pem.h

# Default mount point that picotool / udev expose for a Pico in BOOTSEL
# mode. Override with `make install PICO_MOUNT=/path/to/RPI-RP2`.
PICO_MOUNT    ?= $(firstword $(wildcard /media/$(USER)/RP2350 /media/$(USER)/RPI-RP2 /run/media/$(USER)/RP2350 /run/media/$(USER)/RPI-RP2 /Volumes/RP2350 /Volumes/RPI-RP2))

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

.PHONY: help build init install reconfigure clean distclean

help:
	@echo 'pico2w-wigle — Wi-Fi scanner that uploads to wigle.net'
	@echo
	@echo 'Common targets:'
	@echo '  make init      Configure Wigle credentials + upload Wi-Fi (writes src/secrets.h)'
	@echo '  make build     Compile firmware (.uf2) using the Pico SDK'
	@echo '  make install   Copy the .uf2 onto a Pico 2 W in BOOTSEL mode'
	@echo '  make clean     Remove build artefacts'
	@echo '  make distclean Remove build artefacts AND src/secrets.h'
	@echo
	@echo 'Variables (override on the command line):'
	@echo '  PICO_SDK_PATH  Path to a pico-sdk checkout (default: $$HOME/pico-sdk)'
	@echo '  PICO_BOARD     Board name passed to CMake (default: pico2_w)'
	@echo '  PICO_MOUNT     Where the Pico 2 W mass-storage device is mounted'
	@echo '  BUILD_DIR      CMake build directory (default: build)'

# ---- init -----------------------------------------------------------------

init:
	@scripts/init.sh

# ---- build ----------------------------------------------------------------

build: $(UF2)

$(UF2): $(SECRETS_HEADER) $(CA_HEADER) | $(BUILD_DIR)/CMakeCache.txt
	@cmake --build $(BUILD_DIR) --parallel $(JOBS)
	@echo
	@echo 'Built $(UF2)'

$(BUILD_DIR)/CMakeCache.txt:
	@if [ ! -d "$(PICO_SDK_PATH)" ]; then \
	  echo "ERROR: PICO_SDK_PATH=$(PICO_SDK_PATH) does not exist." >&2; \
	  echo "Install pico-sdk (https://github.com/raspberrypi/pico-sdk) and" >&2; \
	  echo "either symlink it to \$$HOME/pico-sdk or run with" >&2; \
	  echo "  make build PICO_SDK_PATH=/your/pico-sdk" >&2; \
	  exit 1; \
	fi
	@cmake -S . -B $(BUILD_DIR) \
	  -DPICO_BOARD=$(PICO_BOARD) \
	  -DPICO_SDK_PATH=$(PICO_SDK_PATH) \
	  -DCMAKE_BUILD_TYPE=Release

reconfigure:
	rm -rf $(BUILD_DIR)
	@$(MAKE) $(BUILD_DIR)/CMakeCache.txt

$(SECRETS_HEADER) $(CA_HEADER):
	@echo 'ERROR: $@ is missing. Run `make init` first.' >&2
	@exit 1

# ---- install --------------------------------------------------------------

install: $(UF2)
	@if [ -z "$(PICO_MOUNT)" ] || [ ! -d "$(PICO_MOUNT)" ]; then \
	  echo "ERROR: Pico 2 W mass-storage volume not found." >&2; \
	  echo "  Hold BOOTSEL while plugging the board in over USB," >&2; \
	  echo "  then re-run \`make install\`. If your distribution mounts the" >&2; \
	  echo "  volume at a non-default path, pass it explicitly:" >&2; \
	  echo "    make install PICO_MOUNT=/path/to/RP2350" >&2; \
	  exit 1; \
	fi
	@echo "Copying $(UF2) -> $(PICO_MOUNT)/"
	@cp "$(UF2)" "$(PICO_MOUNT)/"
	@sync
	@echo "Done. The Pico 2 W will reboot into the new firmware."

# ---- cleaning -------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR)

distclean: clean
	rm -f $(SECRETS_HEADER) $(CA_HEADER)
