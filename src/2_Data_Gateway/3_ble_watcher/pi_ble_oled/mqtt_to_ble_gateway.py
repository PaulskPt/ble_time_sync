# Copyright (c) 2026 Paulus Schulinck (Github @PaulskPt)
#
# SPDX-License-Identifier: MIT
# 
# Project: ble_ntp_time
#
import paho.mqtt.client as mqtt
import json
import os

# ====================================================================
# CONFIGURATION PARAMETERS
# ====================================================================
MQTT_BROKER = "localhost"
TOPIC_AMBIENT = "sensors/Feath/ambient"

EPOCH_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_epoch.txt"
TEMP_FILE_PATH = "/home/paulsk/pi_ble_oled/latest_temp.txt"

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
        data = json.loads(payload_str)

        # 1. PARSE NESTED EPOCH: Check under the "hd" tracking dictionary
        epoch_val = None
        if "hd" in data and "t" in data["hd"]:
            epoch_val = int(data["hd"]["t"])
        elif "t" in data:
            epoch_val = int(data["t"])

        if epoch_val:
            with open(EPOCH_FILE_PATH, "w") as f:
                f.write(str(epoch_val))
            print(f"[MQTT] Recorded Epoch: {epoch_val}")

        # 2. PARSE HIGH-PRECISION NESTED TEMPERATURE: data["reads"]["t"]["v"]
        if "reads" in data and "t" in data["reads"] and "v" in data["reads"]["t"]:
            temp_raw = data["reads"]["t"]["v"]

            # Format cleanly as "Temp: 28.4 C" matching your Arduino floats schema
            temp_str = f"Temp: {temp_raw} C"
            with open(TEMP_FILE_PATH, "w") as f:
                f.write(temp_str)
            print(f"[MQTT] Parsed Nested Sensor Value: {temp_str}")
        else:
            print(f"[MQTT] data = \"%s\"\n")

    except Exception as e:
        print(f"JSON Parsing or File Write Error: {e}")

def main():
    mqtt_client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message

    print("Connecting to local RPiCM5 MQTT Broker...")
    mqtt_client.connect(MQTT_BROKER, 1883, 60)

    print("Gateway Service initialized. Awaiting background events...")
    mqtt_client.loop_forever()

if __name__ == "__main__":
    main()
