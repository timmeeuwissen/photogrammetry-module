.PHONY: install start venv esp32-deps flash-esp32 server-deps erase reset config build scan

# ESP32 Settings
-include .device.env
PORT ?= $(DEVICE)

select-device:
	@echo "Looking for USB-TTL adapters..."
	@if ! ls /dev/cu.usbserial-* 1>/dev/null 2>&1; then \
		echo "No USB-TTL adapters found. Please check:"; \
		echo "1. USB-TTL adapter is connected"; \
		echo "2. Try a different USB port"; \
		echo "3. Verify adapter drivers are installed"; \
		echo "\nAvailable serial devices:"; \
		ls -1 /dev/cu.* 2>/dev/null || echo "No serial devices found"; \
		exit 1; \
	fi
	@echo "\nFound USB-TTL adapter(s):"
	@ls /dev/cu.usbserial-* 2>/dev/null | nl
	@echo "\nEnter the number of the adapter to use:"
	@read -p "Selection: " num && \
	device=$$(ls /dev/cu.usbserial-* 2>/dev/null | sed -n "$${num}p") && \
	if [ -e "$$device" ]; then \
		echo "DEVICE=$$device" > .device.env && \
		echo "\nSelected device: $$device"; \
		echo "\nWould you like to test the connection? [y/N] "; \
		read -r test && \
		if [ "$$test" = "y" ] || [ "$$test" = "Y" ]; then \
			./test_serial.sh; \
		fi \
	else \
		echo "\nInvalid selection. Please try again.\n" && \
		$(MAKE) select-device; \
	fi

check-device:
	@if [ ! -f .device.env ] || [ ! -e "$$DEVICE" ] || ! echo "$$DEVICE" | grep -q "^/dev/cu.usbserial-"; then \
		echo "No valid USB-TTL adapter configured. Please select a device:"; \
		$(MAKE) select-device; \
	fi
BOARD ?= esp32:esp32:esp32cam
ARDUINO_CLI ?= arduino-cli
UPLOAD_SPEED ?= 115200
CONNECT_SPEED = 9600  # Required for ESP32-CAM HW-297
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
	$(ARDUINO_CLI) config init
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

build: config
	$(ARDUINO_CLI) compile --fqbn $(BOARD) module/module.ino --verbose

flash-esp32: build check-device
	$(ESPTOOL) --port $(PORT) --chip esp32 --baud $(CONNECT_SPEED) \
		--before default_reset --after hard_reset \
		write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
		0x1000 build/module.ino.bootloader.bin \
		0x8000 build/module.ino.partitions.bin \
		0x10000 build/module.ino.bin

install: server-deps esp32-deps config

# Additional targets for ESP32-CAM
monitor: check-device
	$(ARDUINO_CLI) monitor -p $(PORT) -c baudrate=115200

erase: check-device
	$(ESPTOOL) --port $(PORT) --chip esp32 --baud $(CONNECT_SPEED) \
		--before default_reset --after hard_reset erase_flash

reset: check-device
	$(ESPTOOL) --port $(PORT) --chip esp32 --baud $(CONNECT_SPEED) read_mac

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
	@echo "  scan         - Start a new scan"
	@echo "  select-device - Select a serial device to use"
	@echo "  install      - Install all dependencies"
	@echo "  config      - Create config.h from template if it doesn't exist"
	@echo "  start       - Start the server"
	@echo "  flash-esp32 - Compile and flash the ESP32-CAM"
	@echo "  monitor     - Monitor serial output from ESP32-CAM"
	@echo "  erase      - Erase ESP32-CAM flash memory"
	@echo "  clean      - Remove build files and virtual environment"
	@echo ""
	@echo "Configuration:"
	@echo "  1. Run 'make config' to create config.h"
	@echo "  2. Edit module/config.h with your settings"
	@echo "  3. Run 'make install' to install dependencies"
	@echo "  4. Run 'make flash-esp32' to compile and flash"
