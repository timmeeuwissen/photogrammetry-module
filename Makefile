# Colors
RED := \033[0;31m
GREEN := \033[0;32m
YELLOW := \033[0;33m
BLUE := \033[0;34m
PURPLE := \033[0;35m
CYAN := \033[0;36m
WHITE := \033[0;37m
BOLD := \033[1m
RESET := \033[0m

.PHONY: install start venv esp32-deps flash-esp32 server-deps erase reset config build scan cli monitor-camera monitor-controller test-camera test-controller test-system clean-flash clean stop abort

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
	@echo "$(BOLD)Available serial devices:$(RESET)"
	@echo "$(CYAN)USB-TTL Adapters:$(RESET)"
	@ls -1 /dev/cu.usbserial-* 2>/dev/null || echo "$(YELLOW)No USB-TTL adapters found$(RESET)"
	@echo "\n$(CYAN)Other Serial Devices:$(RESET)"
	@ls -1 /dev/cu.* 2>/dev/null | grep -v "usbserial-" || echo "$(YELLOW)No other serial devices found$(RESET)"

select-device:
	@echo "$(BOLD)Available serial devices:$(RESET)"
	@echo "$(CYAN)USB-TTL Adapters:$(RESET)"
	@ls -1 /dev/cu.usbserial-* 2>/dev/null || echo "$(YELLOW)No USB-TTL adapters found$(RESET)"
	@echo "\n$(CYAN)Other Serial Devices:$(RESET)"
	@ls -1 /dev/cu.* 2>/dev/null | grep -v "usbserial-" || echo "$(YELLOW)No other serial devices found$(RESET)"
	@echo "\n$(BOLD)Enter the full path of the device to use$(RESET) (e.g., /dev/cu.usbserial-1410):"
	@read -p "> " device && \
	if [ -e "$$device" ]; then \
		echo "DEVICE=$$device" > $(DEVICE_ENV_FILE) && \
		echo "\n$(GREEN)Selected device: $$device$(RESET)"; \
		echo "\n$(YELLOW)Would you like to test the connection? [y/N]$(RESET) "; \
		read -r test && \
		if [ "$$test" = "y" ] || [ "$$test" = "Y" ]; then \
			./test_serial.sh; \
		fi \
	else \
		echo "\n$(RED)Error: Device $$device does not exist$(RESET)"; \
		exit 1; \
	fi

# Debug target for port
echo-port:
	@echo "$(CYAN)Current port:$(RESET) $(PORT)"

# Ensure port is set
ensure-port:
	@if [ -z "$(PORT)" ]; then \
		echo "$(RED)Error: No device port configured$(RESET)"; \
		echo "$(YELLOW)Current .device.env content:$(RESET)"; \
		cat .device.env 2>/dev/null || echo "$(RED)(.device.env not found)$(RESET)"; \
		exit 1; \
	fi

check-device:
	@if [ ! -f .device.env ]; then \
		echo "$(YELLOW)No device configuration found. Please select a device:$(RESET)"; \
		$(MAKE) select-device; \
		exit 1; \
	fi
	@CURRENT_DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
	if [ ! -e "$$CURRENT_DEVICE" ]; then \
		echo "$(RED)Invalid or missing device: $$CURRENT_DEVICE$(RESET)"; \
		echo "$(YELLOW)Please select a valid device:$(RESET)"; \
		$(MAKE) select-device; \
		exit 1; \
	fi

venv:
	python3 -m venv venv

install: server-deps esp32-deps
	@echo "$(GREEN)All dependencies installed successfully$(RESET)"

server-deps: venv
	@echo "$(CYAN)Installing Python dependencies...$(RESET)"
	. venv/bin/activate && python3 -m pip install flask requests

start: venv
	@echo "$(GREEN)Starting server...$(RESET)"
	. venv/bin/activate && python3 server/app.py

cli: venv
	@echo "$(GREEN)Launching interactive control menu...$(RESET)"
	@. venv/bin/activate && python3 server/cli.py

esp32-deps:
	@echo "$(CYAN)Installing ESP32 dependencies...$(RESET)"
	@if ! $(ARDUINO_CLI) config dump > /dev/null 2>&1; then \
		$(ARDUINO_CLI) config init; \
	fi
	$(ARDUINO_CLI) config add board_manager.additional_urls $(ESP32_URL)
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	$(ARDUINO_CLI) lib install "LiquidCrystal I2C"
	$(ARDUINO_CLI) lib install "ArduinoJson"
	@echo "$(GREEN)ESP32 dependencies installed successfully$(RESET)"
	# WiFi, HTTPClient, WebServer, and esp32-camera are included in the ESP32 board package

config:
	@if [ ! -f camera/config.h ]; then \
		echo "$(CYAN)Creating camera config.h from template...$(RESET)"; \
		cp camera/config.h.example camera/config.h; \
		echo "$(YELLOW)Please edit camera/config.h with your settings$(RESET)"; \
	fi
	@if [ ! -f controller/config.h ]; then \
		echo "$(CYAN)Creating controller config.h from template...$(RESET)"; \
		cp controller/config.h.example controller/config.h; \
		echo "$(YELLOW)Please edit controller/config.h with your settings$(RESET)"; \
	fi

# Build targets
build-camera: config
	@echo "$(CYAN)Building camera firmware...$(RESET)"
	@mkdir -p build
	$(ARDUINO_CLI) compile --fqbn $(CAMERA_BOARD) --output-dir build camera/camera.ino --verbose
	@if [ ! -f build/camera.ino.bin ]; then \
		echo "$(RED)Build failed: Output files not created$(RESET)"; \
		exit 1; \
	fi

build-controller: config
	@echo "$(CYAN)Building controller firmware...$(RESET)"
	@mkdir -p build
	$(ARDUINO_CLI) compile --fqbn $(CONTROLLER_BOARD) --output-dir build controller/controller.ino --verbose
	@if [ ! -f build/controller.ino.bin ]; then \
		echo "$(RED)Build failed: Output files not created$(RESET)"; \
		exit 1; \
	fi

build: build-camera build-controller

reset-port:
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		if [ -e "$$DEVICE" ]; then \
			echo "$(YELLOW)Resetting port $$DEVICE...$(RESET)"; \
			lsof "$$DEVICE" | awk 'NR>1 {print $$2}' | xargs -r kill -9 2>/dev/null || true; \
			stty -f $$DEVICE 115200; \
			echo "$(BOLD)ESP32 Boot Sequence:$(RESET)"; \
			echo "$(CYAN)1. Press and hold BOOT button$(RESET)"; \
			echo "$(CYAN)2. Press and release EN/RST button$(RESET)"; \
			echo "$(CYAN)3. Wait 1 second$(RESET)"; \
			echo "$(CYAN)4. Release BOOT button$(RESET)"; \
			read -p "$(YELLOW)Press Enter when ready to start...$(RESET)"; \
			sleep 3; \
			echo "$(GREEN)Starting upload...$(RESET)"; \
		fi \
	fi

flash-camera: check-device ensure-port reset-port
	@echo "$(CYAN)Uploading camera firmware...$(RESET)"
	@echo "$(BLUE)Using baud rate: $(CONNECT_SPEED)$(RESET)"
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		echo "$(CYAN)Device from .device.env: $$DEVICE$(RESET)"; \
		if [ -e "$$DEVICE" ]; then \
			echo "$(GREEN)Device exists and is accessible$(RESET)"; \
			echo "$(CYAN)Device permissions:$(RESET)"; \
			ls -l $$DEVICE; \
			echo "$(CYAN)Current processes using device:$(RESET)"; \
			lsof $$DEVICE 2>/dev/null || echo "$(GREEN)No processes using device$(RESET)"; \
			echo "\n$(CYAN)Checking build files:$(RESET)"; \
			ls -l build/*.bin 2>/dev/null || echo "$(RED)No build files found - run make build-camera first$(RESET)"; \
			echo "\n$(YELLOW)Attempting upload...$(RESET)"; \
			$(ARDUINO_CLI) upload -p $$DEVICE --fqbn $(CAMERA_BOARD) camera/camera.ino || \
			{ echo "\n$(RED)Upload failed. Troubleshooting steps:$(RESET)"; \
			  echo "$(YELLOW)1. Try pressing the BOOT button while uploading$(RESET)"; \
			  echo "$(YELLOW)2. Check physical connections$(RESET)"; \
			  echo "$(YELLOW)3. Try a different USB port$(RESET)"; \
			  echo "$(YELLOW)4. Try a slower baud rate (current: $(UPLOAD_SPEED))$(RESET)"; \
			  exit 1; }; \
		else \
			echo "$(RED)Error: Device $$DEVICE does not exist$(RESET)"; \
			exit 1; \
		fi \
	else \
		echo "$(RED)Error: .device.env file not found$(RESET)"; \
		exit 1; \
	fi

flash-controller: check-device ensure-port reset-port
	@echo "$(CYAN)Uploading controller firmware...$(RESET)"
	@echo "$(BLUE)Using baud rate: $(CONNECT_SPEED)$(RESET)"
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		echo "$(CYAN)Device from .device.env: $$DEVICE$(RESET)"; \
		if [ -e "$$DEVICE" ]; then \
			echo "$(GREEN)Device exists and is accessible$(RESET)"; \
			echo "$(CYAN)Device permissions:$(RESET)"; \
			ls -l $$DEVICE; \
			echo "$(CYAN)Current processes using device:$(RESET)"; \
			lsof $$DEVICE 2>/dev/null || echo "$(GREEN)No processes using device$(RESET)"; \
			echo "\n$(CYAN)Checking build files:$(RESET)"; \
			ls -l build/*.bin 2>/dev/null || echo "$(RED)No build files found - run make build-controller first$(RESET)"; \
			echo "\n$(YELLOW)Attempting upload...$(RESET)"; \
			$(ESPTOOL) --port $$DEVICE --chip esp32c3 --baud $(UPLOAD_SPEED) \
				write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
				0x0 build/controller.ino.bin || \
			{ echo "\n$(RED)Upload failed. Troubleshooting steps:$(RESET)"; \
			  echo "$(YELLOW)1. Try pressing the BOOT button while uploading$(RESET)"; \
			  echo "$(YELLOW)2. Check physical connections$(RESET)"; \
			  echo "$(YELLOW)3. Try a different USB port$(RESET)"; \
			  echo "$(YELLOW)4. Try a slower baud rate (current: $(UPLOAD_SPEED))$(RESET)"; \
			  exit 1; }; \
		else \
			echo "$(RED)Error: Device $$DEVICE does not exist$(RESET)"; \
			exit 1; \
		fi \
	else \
		echo "$(RED)Error: .device.env file not found$(RESET)"; \
		exit 1; \
	fi

# Clean flash and upload
clean-flash: check-device ensure-port reset-port
	@echo "$(YELLOW)Erasing flash memory...$(RESET)"
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	if [ -f build/controller.ino.bin ]; then \
		echo "$(CYAN)Detected controller firmware - using ESP32-C3 chip$(RESET)"; \
		$(ESPTOOL) --port $$DEVICE --chip esp32c3 --baud $(UPLOAD_SPEED) erase_flash; \
	else \
		echo "$(CYAN)Using default ESP32 chip$(RESET)"; \
		$(ESPTOOL) --port $$DEVICE --chip esp32 --baud $(UPLOAD_SPEED) erase_flash; \
	fi
	@echo "$(GREEN)Flash memory erased$(RESET)"
	@sleep 2

# Monitor targets
monitor-camera: check-device ensure-port
	@echo "$(CYAN)Starting camera monitor...$(RESET)"
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		if [ -e "$$DEVICE" ]; then \
			echo "$(CYAN)Monitoring device: $$DEVICE$(RESET)"; \
			$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED); \
		else \
			echo "$(RED)Error: Device $$DEVICE does not exist$(RESET)"; \
			exit 1; \
		fi \
	else \
		echo "$(RED)Error: .device.env file not found$(RESET)"; \
		exit 1; \
	fi

monitor-controller: check-device ensure-port
	@echo "$(CYAN)Starting controller monitor...$(RESET)"
	@if [ -f .device.env ]; then \
		DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2); \
		if [ -e "$$DEVICE" ]; then \
			echo "$(CYAN)Monitoring device: $$DEVICE$(RESET)"; \
			screen $$DEVICE $(UPLOAD_SPEED); \
		else \
			echo "$(RED)Error: Device $$DEVICE does not exist$(RESET)"; \
			exit 1; \
		fi \
	else \
		echo "$(RED)Error: .device.env file not found$(RESET)"; \
		exit 1; \
	fi

# Test targets
test-camera: check-device ensure-port
	@echo "$(CYAN)Testing camera connection...$(RESET)"
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED) --timeout 5

test-controller: check-device ensure-port
	@echo "$(CYAN)Testing controller connection...$(RESET)"
	@DEVICE=$$(cat .device.env | grep DEVICE | cut -d'=' -f2) && \
	$(ARDUINO_CLI) monitor -p $$DEVICE -c baudrate=$(UPLOAD_SPEED) --timeout 5

test-system: test-camera test-controller
	@echo "$(CYAN)Testing complete system...$(RESET)"
	@curl -s http://localhost:8888/api/status || (echo "$(RED)Server not running$(RESET)" && exit 1)
	@curl -s http://localhost:8889/api/status || (echo "$(RED)Camera not responding$(RESET)" && exit 1)
	@echo "$(GREEN)System test completed successfully$(RESET)"

clean:
	@echo "$(YELLOW)Cleaning build files...$(RESET)"
	rm -rf build/
	rm -rf venv/
	@echo "$(GREEN)Clean completed$(RESET)"

# Server control targets
abort:
	@echo "$(YELLOW)Aborting scan...$(RESET)"
	@if ! curl -s -X POST http://localhost:8888/api/abort > /dev/null; then \
		echo "$(RED)Failed to abort scan. Is the server running?$(RESET)"; \
		exit 1; \
	fi
	@echo "$(GREEN)Scan aborted$(RESET)"

stop:
	@echo "$(YELLOW)Stopping server...$(RESET)"
	@PID=$$(ps aux | grep "[p]ython3 server.py" | awk '{print $$2}') && \
	PORT_PID=$$(lsof -ti:8888) && \
	if [ -n "$$PID" ] || [ -n "$$PORT_PID" ]; then \
		if [ -n "$$PID" ]; then \
			kill $$PID 2>/dev/null || kill -9 $$PID; \
			echo "$(GREEN)Server stopped (PID: $$PID)$(RESET)"; \
		fi; \
		if [ -n "$$PORT_PID" ]; then \
			kill $$PORT_PID 2>/dev/null || kill -9 $$PORT_PID; \
			echo "$(GREEN)Killed process using port 8888 (PID: $$PORT_PID)$(RESET)"; \
		fi \
	else \
		echo "$(YELLOW)Server not running$(RESET)"; \
	fi

# Scan target
scan:
	@echo "$(CYAN)Starting scan process...$(RESET)"
	@if ! curl -s http://localhost:8888/api/status > /dev/null; then \
		echo "$(YELLOW)Server not running. Starting server...$(RESET)"; \
		$(MAKE) start & \
		sleep 2; \
	fi
	@curl -s -X POST http://localhost:8888/api/start
	@echo "\n$(CYAN)Waiting for scan to complete...$(RESET)"
	@while [ "$$(curl -s http://localhost:8888/api/status | grep -o '"status":"[^"]*"' | cut -d'"' -f4)" = "scanning" ]; do \
		echo -n "$(BLUE).$(RESET)"; \
		sleep 1; \
	done
	@echo "\n$(GREEN)Scan completed$(RESET)"

# Help target
help:
	@echo "$(BOLD)Available targets:$(RESET)"
	@echo "$(CYAN)Device Management:$(RESET)"
	@echo "  $(BOLD)list-devices$(RESET)  - List all available serial devices"
	@echo "  $(BOLD)select-device$(RESET) - Select and configure a serial device"
	@echo "  $(BOLD)echo-port$(RESET)     - Show currently configured device port"
	@echo
	@echo "$(CYAN)Build & Flash:$(RESET)"
	@echo "  $(BOLD)build-camera$(RESET)    - Build camera firmware only"
	@echo "  $(BOLD)build-controller$(RESET) - Build controller firmware only"
	@echo "  $(BOLD)build$(RESET)           - Build both firmwares"
	@echo "  $(BOLD)flash-camera$(RESET)    - Flash camera firmware"
	@echo "  $(BOLD)flash-controller$(RESET) - Flash controller firmware"
	@echo "  $(BOLD)clean-flash$(RESET)     - Erase flash memory completely"
	@echo
	@echo "$(CYAN)Monitor & Test:$(RESET)"
	@echo "  $(BOLD)monitor-camera$(RESET)    - Monitor camera output"
	@echo "  $(BOLD)monitor-controller$(RESET) - Monitor controller output"
	@echo "  $(BOLD)test-camera$(RESET)       - Test camera connection"
	@echo "  $(BOLD)test-controller$(RESET)   - Test controller connection"
	@echo "  $(BOLD)test-system$(RESET)      - Test complete system"
	@echo
	@echo "$(CYAN)Server Control:$(RESET)"
	@echo "  $(BOLD)start$(RESET)        - Start the server"
	@echo "  $(BOLD)stop$(RESET)         - Stop the server"
	@echo "  $(BOLD)scan$(RESET)         - Start a new scan"
	@echo "  $(BOLD)abort$(RESET)        - Abort current scan"
	@echo "  $(BOLD)cli$(RESET)          - Launch interactive control menu"
	@echo
	@echo "$(CYAN)Setup:$(RESET)"
	@echo "  $(BOLD)install$(RESET)      - Install all dependencies"
	@echo "  $(BOLD)config$(RESET)       - Create config files from templates"
	@echo "  $(BOLD)clean$(RESET)        - Remove build files and virtual environment"
	@echo
	@echo "$(CYAN)Configuration:$(RESET)"
	@echo "  $(BOLD)1.$(RESET) Run '$(GREEN)make config$(RESET)' to create config files"
	@echo "  $(BOLD)2.$(RESET) Edit camera/config.h and controller/config.h"
	@echo "  $(BOLD)3.$(RESET) Run '$(GREEN)make install$(RESET)' to install dependencies"
	@echo "  $(BOLD)4.$(RESET) Flash both boards:"
	@echo "     a. $(GREEN)make flash-camera$(RESET)"
	@echo "     b. $(GREEN)make flash-controller$(RESET)"
