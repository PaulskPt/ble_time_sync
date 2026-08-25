import asyncio
from bleak import BleakScanner, BleakClient

TARGET_MAC = "C0:07:C9:9D:4B:A9"

async def main():
    print("Running a fast wake-up scan to cache the nRF54 node locally...")
    # Scan for 2 seconds to make sure BlueZ populates the hardware table
    await BleakScanner.discover(timeout=2.0)
    
    print(f"Connecting to [{TARGET_MAC}] to inspect GATT database...")
    try:
        async with BleakClient(TARGET_MAC) as client:
            print("Connected! Fetching Services and Characteristics:\n")
            for service in client.services:
                print(f"Service: {service.uuid}")
                for char in service.characteristics:
                    print(f"  └─ Characteristic: {char.uuid} | Properties: {char.properties}")
    except Exception as e:
        print(f"\nConnection failed: {e}")
        print("Verify your nRF54 board didn't go into power-save mode. Try pressing Reset on it.")

if __name__ == "__main__":
    asyncio.run(main())
