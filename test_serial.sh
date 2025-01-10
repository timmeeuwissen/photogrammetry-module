#!/bin/bash

# Get device from .device.env if it exists
if [ -f .device.env ]; then
    source .device.env
    PORT="$DEVICE"
else
    echo "No device configured. Please run 'make select-device' first."
    exit 1
fi
BAUD="9600"

echo "USB-TTL Serial Adapter Test"
echo "=========================="
echo
echo "This test will verify if your USB-TTL adapter is working correctly."
echo
echo "Step 1: Preparation"
echo "-----------------"
echo "1. Disconnect ALL wires from the USB-TTL adapter"
echo "2. Connect ONLY a jumper wire between:"
echo "   - TX to RX pins on the USB-TTL adapter (loopback)"
echo
echo "Press ENTER when ready..."
read -p ""

if ! ls $PORT > /dev/null 2>&1; then
    echo "ERROR: USB-TTL adapter not detected at $PORT"
    echo "Please check USB connection and try again"
    exit 1
fi

echo
echo "USB-TTL adapter detected at $PORT"
echo "Attempting to send and receive test data..."
echo

# Try to install screen if not present
if ! command -v screen > /dev/null; then
    echo "Installing screen..."
    brew install screen
fi

# Create a test file
echo "TEST_STRING_123" > test_serial.txt

# Start screen in detached mode to send the test string
screen -dmS serial_test $PORT $BAUD
sleep 2
screen -S serial_test -X stuff "$(cat test_serial.txt)"
sleep 1
screen -S serial_test -X quit

# Read the response
echo "Reading serial port..."
timeout 5 cat $PORT > serial_response.txt

if grep -q "TEST_STRING_123" serial_response.txt; then
    echo "SUCCESS: Loopback test passed!"
    echo "The USB-TTL adapter is working correctly."
else
    echo "FAILED: No data received or incorrect data."
    echo "This indicates a problem with the USB-TTL adapter."
    echo
    echo "Troubleshooting steps:"
    echo "1. Try a different USB port"
    echo "2. Verify the jumper wire connection between TX and RX"
    echo "3. Try a different jumper wire"
    echo "4. Check if the USB-TTL adapter is recognized by the system:"
    ls -l /dev/cu.*
fi

# Cleanup
rm -f test_serial.txt serial_response.txt
