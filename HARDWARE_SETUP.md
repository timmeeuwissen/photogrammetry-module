# ESP32-CAM Hardware Setup Guide

## Required Hardware
1. ESP32-CAM module
2. USB-TTL adapter (3.3V logic level)
3. Breadboard
4. Jumper wires
5. LED flash module (optional)

## Connection Steps

### 1. Initial Setup
1. Place the ESP32-CAM on the breadboard
2. Ensure USB-TTL adapter is set to 3.3V mode (if it has a voltage selector)

### 2. Basic Connections
Connect the USB-TTL adapter to the ESP32-CAM:
```
USB-TTL     ESP32-CAM
------      ---------
GND    -->  GND
TX     -->  U0R (GPIO3)  # Cross-connected
RX     -->  U0T (GPIO1)  # Cross-connected
3.3V   -->  5V
```

### 3. Programming Mode Setup
1. Connect GPIO0 to GND (required for flashing)
2. Double-check all connections
3. Connect USB-TTL adapter to computer
4. Verify adapter appears in device list:
   ```bash
   ls /dev/cu.*
   ```
   Look for something like `/dev/cu.usbserial-*`

### 4. Reset Sequence
1. Press and hold RESET button
2. Run flash command
3. Release RESET button when you see "Connecting..."

## Common Issues

### No Serial Port Detected
If `/dev/cu.usbserial-*` is not visible:
1. Unplug and replug the USB-TTL adapter
2. Try a different USB port
3. Try a different USB cable
4. Check if adapter drivers are installed

### Connection Fails
If "No serial data received" error:
1. Verify GPIO0 is properly grounded
2. Check TX/RX are crossed correctly
3. Ensure 3.3V power is stable
4. Try pressing RESET just before flashing

### Power Issues
1. Use a good quality USB port or powered hub
2. Some USB ports may not provide enough power
3. Add a large capacitor (100µF) between 3.3V and GND
4. Try a different USB-TTL adapter

## Testing the Setup

1. Run the serial test script:
   ```bash
   ./test_serial.sh
   ```

2. If test fails:
   - Check all connections
   - Verify USB-TTL adapter functionality
   - Try a different USB port

## Final Checks

Before flashing:
- [ ] All connections secure
- [ ] GPIO0 connected to GND
- [ ] Using 3.3V (not 5V)
- [ ] TX/RX properly crossed
- [ ] USB-TTL adapter visible in device list
- [ ] Reset button accessible

After successful flash:
1. Disconnect power
2. Remove GPIO0 to GND connection
3. Reconnect power for normal operation
