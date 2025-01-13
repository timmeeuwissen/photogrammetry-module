# Dual ESP32 Hardware Setup Guide

## Required Hardware

### Board 1: ESP32-CAM Module
1. ESP32-CAM board
2. USB-TTL adapter (3.3V logic level)
3. Jumper wires

### Board 2: ESP32-C3 Super Mini
1. ESP32-C3 Super Mini board
2. 16x2 I2C LCD Display
3. 28BYJ-48 Stepper Motor with ULN2003 Driver
4. Jumper wires
5. Breadboard

## Connection Diagrams

### ESP32-CAM Connections
Connect USB-TTL adapter to ESP32-CAM for programming:
```
USB-TTL     ESP32-CAM
------      ---------
GND    -->  GND
TX     -->  U0R (GPIO3)  # Cross-connected
RX     -->  U0T (GPIO1)  # Cross-connected
3.3V   -->  5V
```

### ESP32-C3 Super Mini Connections

#### LCD Display (I2C)
```
ESP32-C3    LCD I2C
--------    -------
3.3V    -->  VCC
GND     -->  GND
GPIO5   -->  SDA
GPIO6   -->  SCL
```

#### Stepper Motor (via ULN2003)
```
ESP32-C3    ULN2003
--------    -------
3.3V    -->  VCC
GND     -->  GND
GPIO2   -->  IN1
GPIO3   -->  IN2
GPIO4   -->  IN3
GPIO10  -->  IN4
```

## Programming Mode Setup

### ESP32-CAM
1. Connect GPIO0 to GND for flashing
2. Press and hold RESET button
3. Start upload
4. Release RESET when "Connecting..." appears
5. Remove GPIO0-GND connection after flashing

### ESP32-C3 Super Mini
1. Connect to USB port directly (has built-in USB-Serial)
2. Press and hold BOOT button
3. Press and release RST button
4. Release BOOT button
5. Start upload

## Project Structure
```
photogrammetry-module/
├── camera/           # ESP32-CAM firmware
│   ├── config.h      # Camera configuration
│   └── camera.ino    # Camera firmware
├── controller/       # ESP32-C3 firmware
│   ├── config.h      # Controller configuration
│   └── controller.ino # LCD and stepper control
└── server/          # Python server
    └── server.py    # Web server
```

## Make Targets

### Camera Module (ESP32-CAM)
```bash
# Build camera firmware
make build-camera

# Flash camera firmware
make flash-camera

# Monitor camera output
make monitor-camera
```

### Controller Module (ESP32-C3)
```bash
# Build controller firmware
make build-controller

# Flash controller firmware
make flash-controller

# Monitor controller output
make monitor-controller
```

## Communication Flow

1. Server sends commands to ESP32-C3 controller
2. Controller manages LCD display and stepper motor
3. Controller signals ESP32-CAM for photo capture
4. ESP32-CAM captures and uploads photos to server

## Common Issues

### ESP32-CAM Issues
- No serial port detected:
  1. Check USB-TTL adapter connections
  2. Verify GPIO0 is grounded for flashing
  3. Try different USB ports

### ESP32-C3 Issues
- USB not recognized:
  1. Try different USB cable
  2. Install USB-Serial drivers if needed
  3. Check if board appears in device list

### LCD Issues
- No display:
  1. Verify I2C address (usually 0x27 or 0x3F)
  2. Check power connections
  3. Test I2C connection with scanner

### Stepper Issues
- Motor not moving:
  1. Check ULN2003 power connection
  2. Verify all four control pins
  3. Test with simple step sequence

## Testing Setup

1. Test ESP32-C3 USB connection:
```bash
make test-controller
```

2. Test ESP32-CAM connection:
```bash
make test-camera
```

3. Test complete system:
```bash
make test-system
```

## Final Checklist

### ESP32-CAM
- [ ] Camera module properly seated
- [ ] USB-TTL connections secure
- [ ] Power LED on
- [ ] GPIO0 disconnected from GND after flashing

### ESP32-C3
- [ ] USB connection working
- [ ] LCD displaying startup message
- [ ] Stepper responding to commands
- [ ] All connections secure on breadboard

### System
- [ ] Both boards powered
- [ ] WiFi connection established
- [ ] Server communication working
- [ ] Complete rotation test successful
