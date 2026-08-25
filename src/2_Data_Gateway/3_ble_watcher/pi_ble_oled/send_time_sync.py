# Copyright (c) 2026 Paulus Schulinck (Github @PaulskPt)
#
# SPDX-License-Identifier: MIT
# 
# Project: ble_ntp_time
#
import asyncio
import struct
import os
import time
from bleak import BleakClient, BleakScanner

# Configuration parameters matching your nRF54 node settings
TARGET_NAME = "Paul_OLED_Node"
TIME_EPOCH_UUID = "12345678-1234-5678-1234-56789abcdef1"
EPOCH_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_epoch.txt"

async def send_ble_update(epoch_val):
    """Scans, connects, and sends the 8-byte time packet over BLE"""
    print(f"\nScanning for BLE device named '{TARGET_NAME}'...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: adv.local_name == TARGET_NAME, timeout=8.0
    )
    
    if not device:
        print(f"Error: Could not locate '{TARGET_NAME}' over the air during this pass.")
        return False

    print(f"Connecting to {device.name} at [{device.address}]...")
    try:
        async with BleakClient(device, timeout=12.0) as client:
            print("=== BLE TRANSMISSION PIPELINE LINKED ===")
            
            # Pack the 64-bit integer into a compact 8-byte little-endian binary array
            packed_payload = struct.pack("<Q", epoch_val)
            print(f"Transmitting packed array data bytes: {list(packed_payload)}")
            
            await client.write_gatt_char(TIME_EPOCH_UUID, packed_payload, response=True)
            print(f">>> System clock successfully transmitted over BLE: {epoch_val}")
            
            # Brief pause to ensure clean radio teardown
            await asyncio.sleep(1.0)
            return True
    except Exception as e:
        print(f"BLE Transmission Failed: {e}")
        return False

async def main():
    print("BLE High-Speed File Trigger Active. Watching for fresh MQTT dumps...")
    
    # Store the modification time marker of the file to catch changes instantly
    last_mtime = 0
    if os.path.exists(EPOCH_FILE_PATH):
        last_mtime = os.path.getmtime(EPOCH_FILE_PATH)

    while True:
        if os.path.exists(EPOCH_FILE_PATH):
            try:
                # Fetch the active modification time marker of your file
                current_mtime = os.path.getmtime(EPOCH_FILE_PATH)
                
                # The exact millisecond your gateway writes a fresh payload, mtime shifts!
                if current_mtime != last_mtime:
                    last_mtime = current_mtime  # Clear out old marker
                    
                    # Read the fresh value
                    with open(EPOCH_FILE_PATH, "r") as f:
                        content = f.read().strip()
                    
                    if content:
                        current_file_epoch = int(content)
                        print(f"\n[Trigger Alert] File change detected! Processing value: {current_file_epoch}")
                        
                        # Trigger immediate BLE over-the-air sync pass
                        await send_ble_update(current_file_epoch)
                        
            except Exception as e:
                print(f"File Watch Error: {e}")
        
        # High-precision scan interval: Checks the file state once every 1000ms
        await asyncio.sleep(1.0)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nHigh-Speed BLE File Watcher shut down safely.")
