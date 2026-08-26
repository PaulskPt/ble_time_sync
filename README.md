# ble_time_sync

An advanced, event-driven, cross-protocol time synchronization and environment tracking system. This project bridges a Wi-Fi-based MQTT topology with an ultra-low-energy Bluetooth (BLE) network, delivering an automated, self-healing desktop dashboard with sub-2-second data latency.

> 💡 **Design Context Note:** 
> At first glance, deploying this specific combination of hardware and software layers solely to update the internal real-time clock of an nRF54LM20-DK board might seem extensive. However, this project was architected to seamlessly plug into an **already existing, robust MQTT production environment** consisting of a powered edge publisher and a local broker. 
> 
> Because the active Wi-Fi MQTT Publisher broadcasts a structured data packet every 60 seconds containing an up-to-date NTP epoch timestamp inside its payload header, this project creatively intercepts that existing data stream. The Raspberry Pi CM5 extracts the timestamp on the fly and immediately routes it over the air via a low-energy BLE GATT characteristic write to the nRF54 target, maximizing existing home automation infrastructures with elegant precision.

---

## 📋 System Architecture & Data Flow

```text
 [ESP32-S3 Hub] ──► Reads Internet NTP & Sensor Metrics
       │
       ▼ (Wi-Fi: MQTT PUBLISH to topic 'sensors/Feath/ambient')
       │
 [RPi CM5 Broker] ──► Managed by Local Mosquitto Instance
       │
       ├─► [mqtt_recorder.service] ──► Runs 'mqtt_to_ble_gateway.py'
       │                                     │
       │                                     ▼ (Parses JSON & extracts Epoch)
       │                               [latest_epoch.txt] (Atomic File Cache)
       │                                     ▲
       ├─► [ble_watcher.service] ────────────┘ (Triggers instantly on File Change)
       │         │
       │         ▼ Runs 'send_time_sync.py' (Gated One-Shot Engine)
       │
       ▼ (Wireless BLE GATT Write Protocol Payload)
       │
 [nRF54LM20-DK] ──► Intercepts 8-byte Binary Packet
       │
       ├─► Executes 'sys_clock_settime()' to sync Hardware OS Clocks
       ├─► Processes 10-Year Automated Timezone Lookup (WET / WEST)
       │
       ▼
 [OLED Display Panel] ──► Ticks Date & Local Portuguese Time Every Second!
```

---

## 🛠️ Hardware Stack Summary

| Hardware Platform | Specs & Architecture | Role & Implementation |
| :--- | :--- | :--- |
| **Adafruit Feather ESP32-S3 TFT** *(ID: 5483)* | Tensilica Xtensa LX7 dual-core | **MQTT Publisher (Wi-Fi Core):** Connects to network routers to fetch internet NTP Epoch time and broadcasts structured payloads once a minute. |
| **Pimoroni Multi Sensor Stick** *(ID: PIM745)* | BOSCH BME280 temp, pressure and humidity | **Environmental Sensor Hub:** Monitors ambient temperature, pressure, relative humidity, and VOC properties. |
| **M5Stack Unit RTC** *(ID: U126)* | NXP PCF8563 high-stability RTC | **Local Hardware Clock Tracker:** Maintained by the ESP32-S3 as a highly stable, non-volatile time anchor. |
| **Raspberry Pi Compute Module 5** *(Pi CM5)* | Broadcom BCM2712 Quad-Core @ 2.4GHz | **System Gateway & Broker:** Runs a Linux OS instance executing persistent `systemd` cross-protocol pipelines: `mqtt_recorder.service`, `ble_watcher.service` and a local MQTT broker (Mosquitto). |
| **Nordic nRF54LM20-DK** *(nRF54LM20B)* | ARM Cortex-M33 App Processor | **End-Node Receiver:** Runs the Zephyr RTOS `ble_time_sync` firmware image, parses 10-year timezone matrices, and updates internal clocks. |
| **Adafruit 1.12" OLED Display** | Solomon Systech SH1107 | **Dynamic Visual Interface:** Monochrome 128x128 white pixels over I2C rendering dynamic calendar dates, headers, and time down to the second. |

---

## 💾 Software Components & Implementation

### 1. MQTT Publisher (`ESP32-S3 Arduino Sketch`)
Gathers NTP timestamp tokens and wraps them along with environment data inside a structured JSON layout using `composePayload()`.
* **Header Format Example:** `{"hd": {"ow": "Feath", "de": "Lab", "dc": "BME280", "sc": "meas", "vt": "f", "t": 1787601381}}` where `t` represents the live Epoch timeline.

### 2. Linux Background Infrastructure (`Raspberry Pi CM5`)
Managed by two independent, unbuffered, auto-starting **`systemd`** services to decouple Wi-Fi transactions from the Bluetooth hardware controller:

The Raspberry Pi CM5 serves as the central data gateway, natively hosting a **Mosquitto MQTT Broker** instance to capture incoming Wi-Fi message packets. The orchestration pipeline is managed continuously by two independent, unbuffered, auto-starting **`systemd`** background services to cleanly decouple network data transactions from the local Bluetooth radio hardware controller:

* **Mosquitto MQTT Broker**: 
  Runs as a core system daemon, handling incoming TCP/IP data frames from the ESP32-S3 publisher once a minute and routing them locally with near-zero overhead.
* **`mqtt_recorder.service`** (Runs `mqtt_to_ble_gateway.py`):
  Subscribes directly to the local broker topic `sensors/Feath/ambient`, decodes the payload dictionary, extracts the nested epoch string, and atomically overwrites a flat text file cache (`latest_epoch.txt`).
* **`ble_watcher.service`** (Runs `send_time_sync.py`):
  Uses a high-speed file watcher loop (scanning every 1 second). On modification, it extracts the timestamp, validates a built-in **110-second safety gating filter** to prevent BlueZ hardware overlapping collisions (`InProgress` errors), opens a BLE GATT channel to the target node, transmits an 8-byte little-endian binary array (`uint64_t`), and terminates cleanly.

### 3. Embedded Application Core (`nRF54LM20-DK Application Core`)
Built inside the **nRF Connect SDK v3.4.0 (Zephyr OS v4.4.0)** ecosystem.
* Exposes a 128-bit Vendor-Specific GATT characteristic layout.
* On data reception, intercepts the payload and drops it straight into Zephyr's native real-time operating system clock reference layer via **`sys_clock_settime(CLOCK_REALTIME, &ts)`**.
* Loops through a pre-calculated 10-year transition matrix for the **`Europe/Lisbon`** time zone, automatically shifting between Western European Time (`WET`, GMT+0) and Western European Summer Time (`WEST`, GMT+1).
* Drives the character framebuffer layout on the OLED display glass across rows:
  * **Row 0:** Core Link Status (`BLE: SEARCH`, `BLE: LINKED`, or `STATUS DROP`)
  * **Row 16:** Line Divider (`-----------`)
  * **Row 32:** Dynamic Calendar Date (`yyyy-mm-dd`)
  * **Row 48:** Active Dynamic Time Zone Label (e.g., `Time (WEST)`)
  * **Row 64:** High-Precision Ticking Digital Clock (`HH:MM:SS` updated smoothly every 1000ms)
  * **Row 80 & down:** Wrapped metadata descriptions or user custom data lines.

---

## 🚀 Key Architectural Advantages
* **Decoupled Mechanics:** Caching values through the local file system isolates heavy Wi-Fi communication spikes from the sensitive BLE driver stack.
* **Autonomous & Self-Healing:** The gateway architecture restarts automatically if a power cut or link drops.
* **Microsecond Precision:** Bypassing software timers and setting the underlying operating system clock directly ensures zero clock drift on the end display node.

---

## 🛠️ Installation & Setup

1. **Clone the repository:**
   ```bash
   git clone git clone https://github.com
   cd ble_time_sync
   ```

2. **Build Arduino Sketch and Flash the MQTT Publisher:**
   * Open the provided `.ino` [sketch folder](https://github.com/PaulskPt/ble_time_sync/tree/main/src/1_MQTT_Publisher/Feather_ESP32_S3_TFT_MQTT_w_Pimoroni_2xqwstpad_v5) inside the Arduino IDE or VS Code with the ESP32 platform extension active.
   * Ensure your physical hardware suite is wired securely to the I2C lines (Adafruit Feather ESP32-S3 TFT, Pimoroni Multi Sensor Stick, and M5Stack Unit RTC).
   * Install any required baseline packaging libraries (`ArduinoJson`, `WiFi`, `PubSubClient`, etc.).
   * Configure your local Wi-Fi SSID and Password (file: [secrets.h](https://github.com/PaulskPt/ble_time_sync/blob/main/src/1_MQTT_Publisher/Feather_ESP32_S3_TFT_MQTT_w_Pimoroni_2xqwstpad_v5/secrets.h)), and your Raspberry Pi CM5's static IP address for the target MQTT broker (file: [/data/secrets.json](https://github.com/PaulskPt/ble_time_sync/blob/main/src/1_MQTT_Publisher/Feather_ESP32_S3_TFT_MQTT_w_Pimoroni_2xqwstpad_v5/data/secrets.json)). Under "mqtt" fill-in: "use_broker_local" : 1, (or 2) and fill-in IP-address of: "broker_local1" : `"192.168._.___"` or "broker_local2" : `"192.168._.___"`. Under "wifi" : { "ssid" : "Your_WIFI_SSID", "pass" : "Your_WIFI_PASSWORD" },
   * Compile the sketch image and flash it directly to your **Adafruit Feather ESP32-S3 TFT** board over a USB-C interface link.

3. **Deploy the Raspberry Pi CM5 Gateway Services:**
   * Move the [scripts](https://github.com/PaulskPt/ble_time_sync/tree/main/src/2_Data_Gateway/3_ble_watcher/pi_ble_oled) into `~/pi_ble_oled/` and set up your `.venv` virtual environment with `pip install bleak paho-mqtt`.
   * Copy the [service configuration files](https://github.com/PaulskPt/ble_time_sync/tree/main/src/2_Data_Gateway/1_System_services/etc/systemd/system) into `/etc/systemd/system/`.
   * Initialize them:
     ```bash
     sudo systemctl daemon-reload
     sudo systemctl enable mqtt_recorder.service ble_watcher.service
     sudo systemctl start mqtt_recorder.service ble_watcher.service
     ```
4. **Compile and Flash the nRF54 DK Node:**
   * **Prerequisites (Windows 11 Host):** Ensure the following external Nordic desktop utilities are installed to manage your toolchains and handle physical debugging interface links:
     * **nRF Command-Line Tools** (Provides `nrfjprog` and the baseline SEGGER J-Link USB hardware drivers).
     * **nRF Connect for Desktop**:
       * Install the **Toolchain Manager** application from the central desktop launcher dashboard.
       * Use the Toolchain Manager to install **nRF Connect SDK v3.4.0**. This automated installer creates the core directory structure at `C:\ncs\v3.4.0\` and populates the essential downstream repository subfolders (including `zephyr` and `nrf`).
   * **VS Code Extensions:** Verify that the `nRF Connect for VS Code Extension Pack` and `nRF Connect Extension Pack` components are fully installed and active.
   * **Prepare the Toolchain Environment:** Open a terminal window inside VS Code to configure your local compilation dependencies:
     * **a) Install required tool dependencies via pip:**
       ```powershell
       python -m pip install -r C:\ncs\v3.4.0\zephyr\scripts\requirements.txt
       ```
     * **b) Establish and enter the local Python virtual environment:**
       ```powershell
       Set-ExecutionPolicy -Scope Process -ExecutionPolicy RemoteSigned
       .\.venv\Scripts\Activate.ps1
       ```
     * **c) Append critical Nordic utilities to your path and bind the Zephyr workspace base location:**
       ```powershell
       \$env:PATH = "C:\ncs\toolchains\dcbdc366a1\nrfutil\bin;" + \(env:PATH\)env:ZEPHYR_BASE = "C:\ncs\v3.4.0\zephyr"
       ```
   * **Verify Configuration Arrays:** Open and verify your local runtime parameters and 10-year automated timezone lookup matrices inside [secret.h](https://github.com/PaulskPt/ble_time_sync/tree/main/src/3_End_Node/VSCode/projects/ble_ntp_time/src).
   * **Open the Workspace Repository:** Load your active project folder root directory (e.g., `C:\Users\<User>\...\VSCode\projects\ble_ntp_time`) directly inside VS Code.
   * **Handling Ninja Compiler Background Locks:** If you encounter a background process lock validation fault during a compilation loop pass, execute this line to force-clear stale hanging system hooks instantly:
     ```powershell
     Stop-Process -Name "ninja", "cmake" -Force -ErrorAction SilentlyContinue
     ```
   * **Execute Compilation & Device Deployment:** Build the image matrix and flash the target binary image directly onto your hardware node core registers:
     ```powershell
     west build -b nrf54lm20dk/nrf54lm20b/cpuapp --sysbuild
     west flash
     ```

## Feature Update: MQTT Temperature Filtering & Display Bonus

The system now functions as a dual-purpose node. In addition to pulling the NTP network time, the central gateway subscribes to an MQTT topic to filter live temperature data. This combined payload (NTP Epoch Timestamp + Temperature Value) is packed into a custom structured payload and transmitted over BLE GATT to the nRF54LM20-DK client node to be rendered on the 1.12-inch 128x128 SH1107 OLED screen.

```text
[ MQTT Broker ] ──( Temperature )──┐
                                   ▼
[ NTP Server ] ───( Epoch Time )───► [ Central Gateway ] ──( BLE GATT )──► [ nRF54LM20-DK ] ──► [ OLED Display ]
```

### 1. Data Flow & Processing
1. **MQTT Subscription:** The gateway monitors the designated environment topic (e.g., `tele/sonoff/SENSOR`).
2. **JSON Filtering:** Incoming JSON payloads are parsed to extract the specific temperature value (e.g., `SHT4X.Temperature`).
3. **BLE Payload Construction:** The extracted float value is packed alongside the 4-byte NTP epoch time into a unified byte array structure.

### 2. BLE GATT Custom Characteristic Structure
The combined data is written to the Time/Weather Characteristic using the following byte distribution:

| Byte Offset | Data Type | Field Description |
|---|---|---|
| `0x00 - 0x03` | `uint32_t` | NTP Epoch Timestamp (Little-Endian) |
| `0x04 - 0x07` | `float` | Filtered Temperature Value (IEEE-754 Single-Precision) |

### 3. Client Node Display Layout (nRF54LM20-DK)
Upon receiving the GATT write command, the nRF54 application core parses the payload, updates its internal RTC matrix, and refreshes the SH1107 pixel buffer over I2C (Pins **P1.11** for SDA and **P1.12** for SCL) to render:
* **Line 1:** Synchronized Local Time & Date
* **Line 2:** Real-time filtered Temperature string (e.g., `Temp: 23.5 C`)

---

## 📄 License

This project is licensed under the **MIT License** - see below for details:

```text
MIT License

Copyright (c) 2026 Paulus Schulinck (Github @PaulskPt)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
