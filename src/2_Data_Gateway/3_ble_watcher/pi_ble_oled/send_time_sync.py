# Copyright (c) 2026 Paulus Schulinck (Github @PaulskPt)
#
# SPDX-License-Identifier: MIT
# 
# Project: ble_ntp_time
#
import asyncio
import os
import time
from bleak import BleakClient, BleakScanner

TARGET_NAME = "Paul_OLED_Node"
BLE_UNIFIED_UUID = "12345678-1234-5678-1234-56789abcdef1"

EPOCH_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_epoch.txt"
TEMP_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_temp.txt"

async def main():
    print("BLE Unified Single-Write Watcher Active. Monitoring file shifts...")
    
    # Store explicit modification time tracking markers
    last_epoch_mtime = 0
    last_temp_mtime = 0
    last_sync_time = 0

    # Initialize baselines if files exist
    if os.path.exists(EPOCH_FILE_PATH): last_epoch_mtime = os.path.getmtime(EPOCH_FILE_PATH)
    if os.path.exists(TEMP_FILE_PATH): last_temp_mtime = os.path.getmtime(TEMP_FILE_PATH)

    while True:
        current_time = time.time()
        
        # Enforce our 110-second safety window to keep the BlueZ driver stable
        if (current_time - last_sync_time) >= 110:
            epoch_changed = False
            temp_changed = False
            
            if os.path.exists(EPOCH_FILE_PATH):
                current_epoch_mtime = os.path.getmtime(EPOCH_FILE_PATH)
                if current_epoch_mtime != last_epoch_mtime:
                    epoch_changed = True
                    
            if os.path.exists(TEMP_FILE_PATH):
                current_temp_mtime = os.path.getmtime(TEMP_FILE_PATH)
                if current_temp_mtime != last_temp_mtime:
                    temp_changed = True

            # Trigger ONLY if a fresh data write occurred on disk
            if epoch_changed or temp_changed:
                epoch_val = ""
                temp_str = ""
                
                if os.path.exists(EPOCH_FILE_PATH):
                    with open(EPOCH_FILE_PATH, "r") as f: epoch_val = f.read().strip()
                if os.path.exists(TEMP_FILE_PATH):
                    with open(TEMP_FILE_PATH, "r") as f: temp_str = f.read().strip()
                
                if epoch_val and temp_str:
                    # Strip away "Temp: " and " C" to isolate just the numeric string digits (e.g., "27.0")
                    raw_numeric_temp = temp_str.replace("Temp:", "").replace("C", "").strip()
                    
                    # Construct an ultra-lean 15-character payload string well below the 20-byte MTU limit!
                    combined_payload = f"{epoch_val},{raw_numeric_temp}"
                    print(f"\n[Trigger Alert] Compressed Payload Formatted: \"{combined_payload}\"")

                    # Lock down tracking states immediately BEFORE connecting to prevent looping
                    last_epoch_mtime = os.path.getmtime(EPOCH_FILE_PATH)
                    last_temp_mtime = os.path.getmtime(TEMP_FILE_PATH)
                    last_sync_time = current_time
                    
                    print(f"Scanning for BLE device named '{TARGET_NAME}'...")
                    device = await BleakScanner.find_device_by_filter(
                        lambda d, adv: adv.local_name == TARGET_NAME, timeout=8.0
                    )
                    
                    if device:
                        print(f"Connecting to {device.name} at [{device.address}]...")
                        try:
                            async with BleakClient(device, timeout=12.0) as client:
                                print("=== BLE GATEWAY PIPELINE LINKED ===")
                                await client.write_gatt_char(BLE_UNIFIED_UUID, combined_payload.encode('utf-8'), response=False)
                                print(">>> Unified data packet successfully transmitted over BLE!")
                                await asyncio.sleep(1.0)
                        except Exception as e:
                            print(f"BLE Transmission Failed: {e}")
                    else:
                        print(f"Error: Could not locate '{TARGET_NAME}' over the air during this pass.")
                        
        # Sleep exactly 1 second between loop checks
        await asyncio.sleep(1.0)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nWatcher shut down safely.")
