#!/usr/bin/env python3
"""
Verification script for USIO stealth identification watermark.

Sends a vendor-specific USIO read command to register 0x4954
and verifies the 'FPGA Identification' response blob.

Requirements:
    pip install pyusb
    On Linux, run with sudo or set up udev rules.
"""

import usb.core
import usb.util
import sys
import time

# USIO Device Identity
USIO_VID = 0x0B9A
USIO_PID = 0x0910

# Endpoints
EP_OUT = 0x01
EP_IN = 0x82

# Register to read (0x4954 -> 'IT' in hex)
REG_IDENT = 0x4954
EXPECTED_LEN = 64

# Identification blob (must match firmware usio_driver.c)
EXPECTED_BLOB_START = bytes([
    0x8F, 0x2A, 0x49, 0x54, 0x41, 0x49, 0x4B, 0x4F, 0x00, 0x11, 0x22, 0x33, 0xDE, 0xAD, 0xBE, 0xEF
])

def check_watermark():
    print(f"Searching for USIO device ({USIO_VID:04X}:{USIO_PID:04X})...")
    
    dev = usb.core.find(idVendor=USIO_VID, idProduct=USIO_PID)
    
    if dev is None:
        print("Device not found. Make sure the controller is in USIO mode.")
        return

    print("Device found. Opening...")

    try:
        # Detach kernel driver if necessary
        if dev.is_kernel_driver_active(0):
            print("Detaching kernel driver...")
            dev.detach_kernel_driver(0)
            
        dev.set_configuration()
        
        # Build USIO Read Command (6 bytes)
        # byte 0: CMD (0x10) | Channel (0)
        # byte 1: Checksum (0 for read)
        # byte 2-3: Register (LE)
        # byte 4-5: Length (LE)
        cmd = bytes([
            0x10, 
            0x00, 
            REG_IDENT & 0xFF, (REG_IDENT >> 8) & 0xFF,
            EXPECTED_LEN & 0xFF, (EXPECTED_LEN >> 8) & 0xFF
        ])

        print(f"Sending Read command for register 0x{REG_IDENT:04X}...")
        dev.write(EP_OUT, cmd)

        time.sleep(0.05)

        print(f"Reading {EXPECTED_LEN} bytes from EP 0x{EP_IN:02X}...")
        # Firmware queues a pre-command idle ZLP at usio_open for rpcs3's
        # initial drain. Skip up to 2 empty reads before giving up.
        response = b""
        for _ in range(3):
            response = dev.read(EP_IN, EXPECTED_LEN, timeout=2000)
            if len(response) > 0:
                break

        received_bytes = bytes(response)
        print(f"\n--- RECEIVED RAW HEX ({len(received_bytes)} bytes) ---")
        print(received_bytes.hex(' '))
        print("----------------------------------------\n")

        if received_bytes.startswith(EXPECTED_BLOB_START):
            print("[SUCCESS] Identification blob verified! This is an authentic ITAIKO firmware.")
            # Secret message revealed
            signature = received_bytes[2:8].decode('ascii')
            print(f"Signature detected: {signature}")
        else:
            print("[FAILURE] Identification blob mismatch or not found.")
            
    except usb.core.USBError as e:
        print(f"USB Error: {e}")
        if "Permission denied" in str(e):
            print("\nTry running with 'sudo'.")
    except Exception as e:
        print(f"Error: {e}")
    finally:
        usb.util.dispose_resources(dev)

if __name__ == "__main__":
    check_watermark()
