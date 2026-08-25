import asyncio
from bleak import BleakScanner

async def main():
    print("Initializing Bleak Scanner...")
    print("Scanning the airwaves for 5 seconds for ANY nearby BLE devices...\n")
    
    try:
        devices = await BleakScanner.discover(timeout=5.0)
        
        if not devices:
            print("Scan finished: No BLE devices found in range. Check local traffic.")
            return
            
        print(f"--- Found {len(devices)} BLE Devices ---")
        for d in devices:
            # Print the hardware MAC address and the broadcast name if available
            name = d.name if d.name else "Unknown Device Name"
            print(f"Address: [{d.address}] --> Name: {name}")
            
    except Exception as e:
        print(f"\nBleak Software/Hardware Error: {e}")
        print("Make sure your Raspberry Pi's Bluetooth service is turned on.")

if __name__ == "__main__":
    asyncio.run(main())
