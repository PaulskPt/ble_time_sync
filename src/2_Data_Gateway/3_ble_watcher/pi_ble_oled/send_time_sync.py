import asyncio
import os
import time
from bleak import BleakClient, BleakScanner

TARGET_NAME = "Paul_OLED_Node"
BLE_UNIFIED_UUID = "12345678-1234-5678-1234-56789abcdef1"

EPOCH_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_epoch.txt"
TEMP_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_temp.txt"

async def main():
    print("BLE Unified High-Speed Watcher Active. Monitoring file shifts dynamically...")
    
    # Initialize baseline modification time tracking markers to current disk state
    last_epoch_mtime = os.path.getmtime(EPOCH_FILE_PATH) if os.path.exists(EPOCH_FILE_PATH) else 0
    last_temp_mtime = os.path.getmtime(TEMP_FILE_PATH) if os.path.exists(TEMP_FILE_PATH) else 0
    last_sync_time = 0

    while True:
        current_time = time.time()
        epoch_changed = False
        temp_changed = False
        
        # 1. ALWAYS monitor and catch file shifts instantly to keep memory perfectly aligned
        if os.path.exists(EPOCH_FILE_PATH):
            current_epoch_mtime = os.path.getmtime(EPOCH_FILE_PATH)
            if current_epoch_mtime != last_epoch_mtime:
                epoch_changed = True
                last_epoch_mtime = current_epoch_mtime # Clear memory marker instantly
                
        if os.path.exists(TEMP_FILE_PATH):
            current_temp_mtime = os.path.getmtime(TEMP_FILE_PATH)
            if current_temp_mtime != last_temp_mtime:
                temp_changed = True
                last_temp_mtime = current_temp_mtime # Clear memory marker instantly

        # 2. Short 5-second anti-thrashing gate to protect the BlueZ hardware stack
        if (epoch_changed or temp_changed) and (current_time - last_sync_time) >= 5:
            epoch_val = ""
            temp_str = ""
            
            if os.path.exists(EPOCH_FILE_PATH):
                with open(EPOCH_FILE_PATH, "r") as f: epoch_val = f.read().strip()
            if os.path.exists(TEMP_FILE_PATH):
                with open(TEMP_FILE_PATH, "r") as f: temp_str = f.read().strip()
            
            if epoch_val and temp_str:
                # Strip down labels to preserve strict MTU frames
                raw_numeric_temp = temp_str.replace("Temp:", "").replace("C", "").strip()
                combined_payload = f"{epoch_val},{raw_numeric_temp}"
                
                print(f"\n[Trigger Alert] Fresh MQTT Payload Intercepted: \"{combined_payload}\"")
                last_sync_time = current_time
                
                print(f"Scanning for BLE device named '{TARGET_NAME}'...")
                device = await BleakScanner.find_device_by_filter(
                    lambda d, adv: adv.local_name == TARGET_NAME, timeout=5.0
                )
                
                if device:
                    print(f"Connecting to {device.name} at [{device.address}]...")
                    try:
                        async with BleakClient(device, timeout=8.0) as client:
                            print("=== BLE GATEWAY PIPELINE LINKED ===")
                            await client.write_gatt_char(BLE_UNIFIED_UUID, combined_payload.encode('utf-8'), response=False)
                            print(">>> Real-time data successfully transmitted over BLE!")
                            await asyncio.sleep(0.5)
                    except Exception as e:
                        print(f"BLE Transmission Failed: {e}")
                else:
                    print(f"Error: Could not locate '{TARGET_NAME}' over the air during this window.")
                        
        await asyncio.sleep(1.0)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nWatcher shut down safely.")
