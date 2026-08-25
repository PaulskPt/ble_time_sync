import paho.mqtt.client as mqtt
import json
import os

# ====================================================================
# CONFIGURATION PARAMETERS
# ====================================================================
MQTT_BROKER = "localhost"
TOPIC_AMBIENT = "sensors/Feath/ambient"

# The local file that will act as the data bridge between your scripts
EPOCH_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_epoch.txt"

# ====================================================================
# MQTT BACKGROUND CALLBACK HANDLERS (Modern API Version 2)
# ====================================================================
def on_connect(client, userdata, flags, rc, properties=None):
    print(f"Connected to local MQTT Broker with result code {rc}")
    client.subscribe(TOPIC_AMBIENT)
    print(f"Successfully subscribed to topic: '{TOPIC_AMBIENT}'")

def on_message(client, userdata, msg):
    payload_str = msg.payload.decode('utf-8').strip()
    
    try:
        # Parse the JSON string payload directly into a python dictionary layout
        data = json.loads(payload_str)
        
        # Dig down into the dictionary to find the nested NTP epoch value [t]
        epoch_val = None
        if "hd" in data and "t" in data["hd"]:
            epoch_val = int(data["hd"]["t"])
        elif "t" in data:
            epoch_val = int(data["t"])

        if epoch_val:
            print(f"\n[MQTT Event] Parsed fresh Epoch: {epoch_val}")
            
            # Atomically write the value into your tracking file
            with open(EPOCH_FILE_PATH, "w") as f:
                f.write(str(epoch_val))
            print(f"  -> Successfully recorded to: {os.path.basename(EPOCH_FILE_PATH)}")
            
    except Exception as e:
        print(f"JSON Parsing or File Write Error: {e}")

# ====================================================================
# MAIN PROCESS ENTRY POINT
# ====================================================================
def main():
    mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    print("Connecting to local RPiCM5 MQTT Broker...")
    mqtt_client.connect(MQTT_BROKER, 1883, 60)
    
    # Run the continuous block forever to record incoming data points
    print("Gateway Service initialized. Awaiting background events...")
    mqtt_client.loop_forever()

if __name__ == "__main__":
    main()
