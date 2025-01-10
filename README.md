# ESP32-CAM Photogrammetry Module

This project consists of an ESP32-CAM module for capturing images and a server component for processing them.

## Configuration

The project uses a configuration file system to manage settings:

1. Copy the template configuration:
   ```bash
   make config
   ```
   This will create `module/config.h` from `module/config.h.example`

2. Edit `module/config.h` with your settings:
   - WiFi credentials (SSID and password)
   - Server API URL
   - Pin configurations

## Installation

1. Install all dependencies:
   ```bash
   make install
   ```

2. Start the server:
   ```bash
   make start
   ```

3. Flash the ESP32-CAM:
   ```bash
   make flash-esp32
   ```

## Hardware Setup

### ESP32-CAM Connections for Flashing
- GND → USB-TTL GND
- U0R (GPIO3) → USB-TTL TX
- U0T (GPIO1) → USB-TTL RX
- 5V → USB-TTL 3.3V
- GPIO0 → GND (only during flashing)

### Monitoring
Monitor serial output:
```bash
make monitor
```

## Available Commands

- `make help` - Show available commands
- `make config` - Create configuration file from template
- `make install` - Install all dependencies
- `make start` - Start the server
- `make flash-esp32` - Compile and flash the ESP32-CAM
- `make monitor` - Monitor serial output
- `make erase` - Erase ESP32-CAM flash memory
- `make clean` - Remove build files and virtual environment

## Troubleshooting

If you encounter flashing issues:
1. Verify all connections are secure
2. Ensure GPIO0 is connected to GND during flashing
3. Try a lower baud rate by setting `CONNECT_SPEED` in Makefile
4. Check the USB-TTL adapter is working with `./test_serial.sh`
5. See detailed instructions in `flash_instructions.txt`
