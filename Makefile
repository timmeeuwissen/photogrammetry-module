.PHONY: install start venv esp32-deps flash-esp32 server-deps erase reset config build scan

# ESP32 Settings
-include .device.env
PORT := $(shell if [ -f .device.env ]; then grep DEVICE .device.env | cut -d'=' -f2; fi)

# Serial device configuration
DEVICE_ENV_FILE := .device.env

list-devices:
	@echo "Available serial devices:"
	@echo "USB-TTL Adapters:"
	@ls -1 /dev/cu.usbserial-* 2>/dev/null || echo "No USB-TTL adapters found"
	@echo "\nOther Serial Devices:"
	@ls -1 /dev/cu.* 2>/dev/null | grep -v "usbserial-" || echo "No other serial devices found"

select-device: list-devices
	@if ! ls /dev/cu.usbserial-* 1>/dev/null 2>&1; then \
		echo "\nNo USB-TTL adapters found. Please check:"; \
		echo "1. USB-TTL adapter is connected"; \
		echo "2. Try a different USB port"; \
		echo "3. Verify adapter drivers are installed"; \
		exit 1; \
	fi
	@echo "\nSelect a USB-TTL adapter:"
	@ls /dev/cu.usbserial-* | nl
	@read -p "Selection [1-$$(ls /dev/cu.usbserial-* | wc -l | tr -d ' ')]: " num && \
	device=$$(ls /dev/cu.usbserial-* | sed -n "$${num}p") && \
	if [ -e "$$device" ]; then \
		echo "DEVICE=$$device" > $(DEVICE_ENV_FILE) && \
		echo "\nSelected device: $$device"; \
		echo "\nWould you like to test the connection? [y/N] "; \
		read -r test && \
		if [ "$$test" = "y" ] || [ "$$test" = "Y" ]; then \
			./test_serial.sh; \
		fi \
	else \
		echo "\nInvalid selection"; \
		exit 1; \
	fi

# Debug target for port
echo-port:
	@echo $(PORT)

# Ensure port is set
ensure-port:
	@if [ -z "$(PORT)" ]; then \
		echo "Error: No device port configured"; \
		echo "Current .device.env content:"; \
		cat .device.env 2>/dev/null || echo "(.device.env not found)"; \
		exit 1; \
	fi

check-device:
	@if [ ! -f .device.env ]; then \
		echo "No device configuration found. Please select a device:"; \
		$(MAKE) select-device; \
		exit 1; \
	fi
	@CURRENT_DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
	if [ ! -e "$$CURRENT_DEVICE" ] || ! echo "$$CURRENT_DEVICE" | grep -q "^/dev/cu.usbserial-"; then \
		echo "Invalid or missing device: $$CURRENT_DEVICE"; \
		echo "Please select a valid device:"; \
		$(MAKE) select-device; \
		exit 1; \
	fi

BOARD ?= esp32:esp32:esp32cam
ARDUINO_CLI ?= arduino-cli
UPLOAD_SPEED ?= 115200
CONNECT_SPEED = 9600  # Medium baud rate for balance of speed and stability
ESPTOOL = esptool.py

# Config files
CONFIG_TEMPLATE = module/config.h.example
CONFIG_FILE = module/config.h

# ESP32 board URL
ESP32_URL = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

venv:
	python3 -m venv venv

server-deps: venv
	. venv/bin/activate && python3 -m pip install flask

start: venv
	. venv/bin/activate && python3 server.py

esp32-deps:
	@if ! $(ARDUINO_CLI) config dump > /dev/null 2>&1; then \
		$(ARDUINO_CLI) config init; \
	fi
	$(ARDUINO_CLI) config add board_manager.additional_urls $(ESP32_URL)
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	$(ARDUINO_CLI) lib install "WiFi"
	$(ARDUINO_CLI) lib install "HTTPClient"
	$(ARDUINO_CLI) lib install "LiquidCrystal I2C"
	# esp32-camera is included in the ESP32 board package

config:
	@if [ ! -f $(CONFIG_FILE) ]; then \
		echo "Creating config.h from template..."; \
		cp $(CONFIG_TEMPLATE) $(CONFIG_FILE); \
		echo "Please edit $(CONFIG_FILE) with your settings"; \
		exit 1; \
	fi

# Build targets
build-esp32: config
	@echo "Building firmware..."
	$(ARDUINO_CLI) compile --fqbn $(BOARD) module/module.ino --verbose

reset-port:
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		if [ -e "$$DEVICE" ]; then \
			echo "Resetting port $$DEVICE..."; \
			lsof "$$DEVICE" | awk 'NR>1 {print $$2}' | xargs -r kill -9 2>/dev/null || true; \
			stty -f $$DEVICE 115200; \
			echo "ESP32 Boot Sequence:"; \
			echo "1. Press and hold BOOT button"; \
			echo "2. Press and release EN/RST button"; \
			echo "3. Wait 1 second"; \
			echo "4. Release BOOT button"; \
			read -p "Press Enter when ready to start..."; \
			sleep 3; \
			echo "Starting upload..."; \
		fi \
	fi

upload-esp32: check-device ensure-port reset-port
	@echo "Uploading firmware..."
	@echo "Using baud rate: $(CONNECT_SPEED)"
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		echo "Device from .device.env: $$DEVICE"; \
		if [ -e "$$DEVICE" ]; then \
			echo "Device exists and is accessible"; \
			echo "Device permissions:"; \
			ls -l $$DEVICE; \
			echo "Current processes using device:"; \
			lsof $$DEVICE 2>/dev/null || echo "No processes using device"; \
			echo "\nChecking build files:"; \
			ls -l build/*.bin 2>/dev/null || echo "No build files found - run make build-esp32 first"; \
			echo "\nAttempting upload..."; \
			PYTHONPATH=/Users/timmeeuwissen/Library/Arduino15/packages/esp32/tools/esptool_py/4.9.dev3 \
			$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(UPLOAD_SPEED) \
				write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
				0x1000 build/module.ino.bootloader.bin \
				0x8000 build/module.ino.partitions.bin \
				0x10000 build/module.ino.bin || \
			{ echo "\nUpload failed. Troubleshooting steps:"; \
			  echo "1. Try pressing the BOOT button while uploading"; \
			  echo "2. Check physical connections"; \
			  echo "3. Try a different USB port"; \
			  echo "4. Try a slower baud rate (current: $(UPLOAD_SPEED))"; \
			  exit 1; }; \
		else \
			echo "Error: Device $$DEVICE does not exist"; \
			exit 1; \
		fi \
	else \
		echo "Error: .device.env file not found"; \
		exit 1; \
	fi

# Clean flash and upload
clean-flash: check-device ensure-port reset-port
	@echo "Erasing flash memory..."
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(UPLOAD_SPEED) erase_flash
	@echo "Flash memory erased"
	@sleep 2

flash-esp32: check-device ensure-port reset-port clean-flash build-esp32 upload-esp32
	@echo "Build and upload complete"

# Upload firmware only (requires previous build)
upload-esp32-only: check-device ensure-port reset-port
	@echo "Uploading firmware only..."
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(UPLOAD_SPEED) \
		write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
		0x1000 build/module.ino.bootloader.bin \
		0x8000 build/module.ino.partitions.bin \
		0x10000 build/module.ino.bin

install: server-deps esp32-deps config

# Additional targets for ESP32-CAM
monitor: check-device ensure-port
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED)

erase: check-device ensure-port
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(CONNECT_SPEED) erase_flash

reset: check-device ensure-port
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(CONNECT_SPEED) read_mac

clean:
	rm -rf build/
	rm -rf venv/

# Scan target
scan:
	@echo "Starting scan process..."
	@if ! curl -s http://localhost:8888/api/status > /dev/null; then \
		echo "Server not running. Starting server..."; \
		$(MAKE) start & \
		sleep 2; \
	fi
	@curl -s -X POST http://localhost:8888/api/start
	@echo "\nWaiting for scan to complete..."
	@while [ "$$(curl -s http://localhost:8888/api/status | grep -o '"status":"[^"]*"' | cut -d'"' -f4)" = "scanning" ]; do \
		echo -n "."; \
		sleep 1; \
	done
	@echo "\nScan completed"

# Help target
help:
	@echo "Available targets:"
	@echo "Device Management:"
	@echo "  list-devices  - List all available serial devices"
	@echo "  select-device - Select and configure a USB-TTL adapter"
	@echo "  echo-port     - Show currently configured device port"
	@echo
	@echo "Build & Flash:"
	@echo "  build-esp32     - Build firmware only"
	@echo "  upload-esp32    - Upload firmware to device (with checks)"
	@echo "  upload-esp32-only - Upload firmware without rebuilding"
	@echo "  clean-flash    - Erase flash memory completely"
	@echo "  flash-esp32     - Clean flash, build and upload firmware"
	@echo "  erase          - Erase ESP32-CAM flash memory"
	@echo "  monitor        - Monitor serial output from ESP32-CAM"
	@echo
	@echo "Server Control:"
	@echo "  start        - Start the server"
	@echo "  scan         - Start a new scan"
	@echo
	@echo "Setup:"
	@echo "  install      - Install all dependencies"
	@echo "  config       - Create config.h from template"
	@echo "  clean        - Remove build files and virtual environment"
	@echo ""
	@echo "Configuration:"
	@echo "  1. Run 'make config' to create config.h"
	@echo "  2. Edit module/config.h with your settings"
	@echo "  3. Run 'make install' to install dependencies"
	@echo "  4. Run 'make flash-esp32' to compile and flash"
