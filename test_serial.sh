#!/bin/bash

# Simple script to test serial connection
# Usage: ./test_serial.sh /dev/cu.usbserial-*

if [ -f .device.env ]; then
    DEVICE=$(cat .device.env | grep DEVICE | cut -d'=' -f2)
    if [ -e "$DEVICE" ]; then
        echo "Testing connection to $DEVICE..."
        stty -f $DEVICE 115200
        echo "Device info:"
        ls -l $DEVICE
        echo "Current processes using device:"
        lsof $DEVICE 2>/dev/null || echo "No processes using device"
        echo "Testing ESP32 connection..."
        esptool.py --port $DEVICE --chip esp32 --baud 115200 read_mac
    else
        echo "Error: Device $DEVICE does not exist"
        exit 1
    fi
else
    echo "Error: No device configured. Please run 'make select-device' first."
    exit 1
fi
