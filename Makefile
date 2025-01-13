.PHONY: install start venv esp32-deps flash-esp32 server-deps erase reset config build scan

# Board Settings
CAMERA_BOARD ?= esp32:esp32:esp32cam
CONTROLLER_BOARD ?= esp32:esp32:esp32c3
ARDUINO_CLI ?= arduino-cli
UPLOAD_SPEED ?= 115200
CONNECT_SPEED = 9600  # Medium baud rate for balance of speed and stability
ESPTOOL = esptool.py

# Serial device configuration
DEVICE_ENV_FILE := .device.env
-include .device.env
PORT := $(shell if [ -f .device.env ]; then grep DEVICE .device.env | cut -d'=' -f2; fi)

# ESP32 board URL
ESP32_URL = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

list-devices:
	@echo "Available serial devices:"
	@echo "USB-TTL Adapters:"
	@ls -1 /dev/cu.usbserial-* 2>/dev/null || echo "No USB-TTL adapters found"
	@echo "\nOther Serial Devices:"
	@ls -1 /dev/cu.* 2>/dev/null | grep -v "usbserial-" || echo "No other serial devices found"

select-device:
	@echo "Available serial devices:"
	@echo "USB-TTL Adapters:"
	@ls -1 /dev/cu.usbserial-* 2>/dev/null || echo "No USB-TTL adapters found"
	@echo "\nOther Serial Devices:"
	@ls -1 /dev/cu.* 2>/dev/null | grep -v "usbserial-" || echo "No other serial devices found"
	@echo "\nEnter the full path of the device to use (e.g., /dev/cu.usbserial-1410):"
	@read -p "> " device && \
	if [ -e "$$device" ]; then \
		echo "DEVICE=$$device" > $(DEVICE_ENV_FILE) && \
		echo "\nSelected device: $$device"; \
		echo "\nWould you like to test the connection? [y/N] "; \
		read -r test && \
		if [ "$$test" = "y" ] || [ "$$test" = "Y" ]; then \
			./test_serial.sh; \
		fi \
	else \
		echo "\nError: Device $$device does not exist"; \
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
	if [ ! -e "$$CURRENT_DEVICE" ]; then \
		echo "Invalid or missing device: $$CURRENT_DEVICE"; \
		echo "Please select a valid device:"; \
		$(MAKE) select-device; \
		exit 1; \
	fi

venv:
	python3 -m venv venv

server-deps: venv
	. venv/bin/activate && python3 -m pip install flask

start: venv
	. venv/bin/activate && python3 server/server.py

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
	$(ARDUINO_CLI) lib install "WebServer"
	# esp32-camera is included in the ESP32 board package

config:
	@if [ ! -f camera/config.h ]; then \
		echo "Creating camera config.h from template..."; \
		cp camera/config.h.example camera/config.h; \
		echo "Please edit camera/config.h with your settings"; \
	fi
	@if [ ! -f controller/config.h ]; then \
		echo "Creating controller config.h from template..."; \
		cp controller/config.h.example controller/config.h; \
		echo "Please edit controller/config.h with your settings"; \
	fi

# Build targets
build-camera: config
	@echo "Building camera firmware..."
	$(ARDUINO_CLI) compile --fqbn $(CAMERA_BOARD) camera/camera.ino --verbose

build-controller: config
	@echo "Building controller firmware..."
	$(ARDUINO_CLI) compile --fqbn $(CONTROLLER_BOARD) controller/controller.ino --verbose

build: build-camera build-controller

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

flash-camera: check-device ensure-port reset-port
	@echo "Uploading camera firmware..."
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
			ls -l build/*.bin 2>/dev/null || echo "No build files found - run make build-camera first"; \
			echo "\nAttempting upload..."; \
			PYTHONPATH=/Users/timmeeuwissen/Library/Arduino15/packages/esp32/tools/esptool_py/4.9.dev3 \
			$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(UPLOAD_SPEED) \
				write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
				0x1000 build/camera.ino.bootloader.bin \
				0x8000 build/camera.ino.partitions.bin \
				0x10000 build/camera.ino.bin || \
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

flash-controller: check-device ensure-port
	@echo "Uploading controller firmware..."
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(CONTROLLER_BOARD) controller/controller.ino

# Clean flash and upload
clean-flash: check-device ensure-port reset-port
	@echo "Erasing flash memory..."
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(UPLOAD_SPEED) erase_flash
	@echo "Flash memory erased"
	@sleep 2

# Monitor targets
monitor-camera: check-device ensure-port
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED)

monitor-controller: check-device ensure-port
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED)

# Test targets
test-camera: check-device ensure-port
	@echo "Testing camera connection..."
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED) --timeout 5

test-controller: check-device ensure-port
	@echo "Testing controller connection..."
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED) --timeout 5

test-system: test-camera test-controller
	@echo "Testing complete system..."
	@curl -s http://localhost:8888/api/status || (echo "Server not running" && exit 1)
	@curl -s http://localhost:8889/api/status || (echo "Camera not responding" && exit 1)

clean:
	rm -rf build/
	rm -rf venv/

# Server control targets
abort:
	@echo "Aborting scan..."
	@if ! curl -s -X POST http://localhost:8888/api/abort > /dev/null; then \
		echo "Failed to abort scan. Is the server running?"; \
		exit 1; \
	fi
	@echo "Scan aborted"

stop:
	@echo "Stopping server..."
	@PID=$$(ps aux | grep "[p]ython3 server.py" | awk '{print $$2}') && \
	PORT_PID=$$(lsof -ti:8888) && \
	if [ -n "$$PID" ] || [ -n "$$PORT_PID" ]; then \
		if [ -n "$$PID" ]; then \
			kill $$PID 2>/dev/null || kill -9 $$PID; \
			echo "Server stopped (PID: $$PID)"; \
		fi; \
		if [ -n "$$PORT_PID" ]; then \
			kill $$PORT_PID 2>/dev/null || kill -9 $$PORT_PID; \
			echo "Killed process using port 8888 (PID: $$PORT_PID)"; \
		fi \
	else \
		echo "Server not running"; \
	fi

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
	@echo "  select-device - Select and configure a serial device"
	@echo "  echo-port     - Show currently configured device port"
	@echo
	@echo "Build & Flash:"
	@echo "  build-camera    - Build camera firmware only"
	@echo "  build-controller - Build controller firmware only"
	@echo "  build           - Build both firmwares"
	@echo "  flash-camera    - Flash camera firmware"
	@echo "  flash-controller - Flash controller firmware"
	@echo "  clean-flash     - Erase flash memory completely"
	@echo
	@echo "Monitor & Test:"
	@echo "  monitor-camera    - Monitor camera output"
	@echo "  monitor-controller - Monitor controller output"
	@echo "  test-camera       - Test camera connection"
	@echo "  test-controller   - Test controller connection"
	@echo "  test-system      - Test complete system"
	@echo
	@echo "Server Control:"
	@echo "  start        - Start the server"
	@echo "  stop         - Stop the server"
	@echo "  scan         - Start a new scan"
	@echo "  abort        - Abort current scan"
	@echo
	@echo "Setup:"
	@echo "  install      - Install all dependencies"
	@echo "  config       - Create config files from templates"
	@echo "  clean        - Remove build files and virtual environment"
	@echo
	@echo "Configuration:"
	@echo "  1. Run 'make config' to create config files"
	@echo "  2. Edit camera/config.h and controller/config.h"
	@echo "  3. Run 'make install' to install dependencies"
	@echo "  4. Flash both boards:"
	@echo "     a. make flash-camera"
	@echo "     b. make flash-controller"
