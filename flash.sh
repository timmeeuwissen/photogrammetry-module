#!/bin/bash

# Configuration
PORT="/dev/cu.usbserial-1110"
BAUD="4800"  # Even lower baud rate for maximum reliability
CHIP="esp32"

echo "ESP32-CAM Flash Script (Connection Test Method)"
echo "=============================================="
echo
echo "Step 1: Connection Test"
echo "-------------------"
echo "1. Disconnect everything"
echo "2. Connect ONLY these wires first:"
echo "   - USB-TTL GND  -> ESP32-CAM GND"
echo "   - USB-TTL TX   -> ESP32-CAM U0R (GPIO3)"
echo "   - USB-TTL RX   -> ESP32-CAM U0T (GPIO1)"
echo
echo "Press ENTER after making these connections..."
read -p ""

# Test if USB-TTL is detected
if ! ls $PORT > /dev/null 2>&1; then
    echo "ERROR: USB-TTL adapter not detected at $PORT"
    echo "Please check USB connection and try again"
    exit 1
fi

echo
echo "USB-TTL adapter detected successfully!"
echo
echo "Step 2: Power and Boot Mode"
echo "------------------------"
echo "Now connect the remaining wires:"
echo "1. Connect GPIO0 to GND (Boot mode)"
echo "2. Finally, connect USB-TTL 3.3V -> ESP32-CAM 5V (Power)"
echo
echo "Press ENTER after making these connections..."
read -p ""

echo
echo "Step 3: Reset Sequence"
echo "-------------------"
echo "1. Press and hold RESET button"
echo "2. Press ENTER (keep holding RESET)"
echo "3. When you see 'Connecting...', release RESET"
read -p ""

echo
echo "Attempting to connect and erase flash..."
esptool.py --port $PORT --chip $CHIP --baud $BAUD \
    --connect-attempts 10 \
    erase_flash

echo
echo "Step 4: Final Flash Attempt"
echo "-----------------------"
echo "1. Press and hold RESET again"
echo "2. Press ENTER (keep holding RESET)"
echo "3. When you see 'Connecting...', release RESET"
read -p ""

echo
echo "Attempting to flash firmware..."
esptool.py --port $PORT --chip $CHIP --baud $BAUD \
    --connect-attempts 10 \
    write_flash -z --flash_mode dio --flash_freq 40m --flash_size detect \
    0x1000 build/module.ino.bootloader.bin \
    0x8000 build/module.ino.partitions.bin \
    0x10000 build/module.ino.bin

echo
echo "If flashing failed, verify these common issues:"
echo "1. Connections:"
echo "   - TX/RX are crossed (USB-TTL TX → ESP32 RX)"
echo "   - All GND connections are solid"
echo "   - GPIO0 is firmly connected to GND"
echo "2. Power:"
echo "   - Using 3.3V (not 5V) for logic levels"
echo "   - USB port can supply enough current"
echo "3. Timing:"
echo "   - Release RESET right when you see 'Connecting...'"
echo "4. Hardware:"
echo "   - Try a different USB port"
echo "   - Check for loose or damaged wires"
echo "   - Verify USB-TTL adapter functionality"
