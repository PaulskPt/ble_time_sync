# ble_time_sync

An advanced, event-driven, cross-protocol time synchronization and environment tracking ecosystem. This project bridges a Wi-Fi-based MQTT topology with an ultra-low-energy Bluetooth (BLE) network, delivering an automated, self-healing desktop dashboard with sub-2-second data latency.

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
| **Raspberry Pi Compute Module 5** *(Pi CM5)* | Broadcom BCM2712 Quad-Core @ 2.4GHz | **Smart System Gateway:** Runs a headless Linux OS instance executing persistent `systemd` cross-protocol pipelines and a local Mosquitto broker. |
| **Nordic nRF54LM20-DK** *(nRF54LM20B)* | ARM Cortex-M33 App Processor | **End-Node Receiver:** Runs the Zephyr RTOS `ble_time_sync` firmware image, parses 10-year timezone matrices, and updates internal clocks. |
| **Adafruit 1.12" OLED Display** | Solomon Systech SH1107 | **Dynamic Visual Interface:** Monochrome 128x128 white pixels over I2C rendering dynamic calendar dates, headers, and time down to the second. |

---

## 💾 Software Components & Implementation

### 1. Edge Publisher (`ESP32-S3 Arduino Sketch`)
Gathers NTP timestamp tokens and wraps them along with environment data inside a structured JSON layout using `composePayload()`.
* **Header Format Example:** `{"hd": {"ow": "Feath", "de": "Lab", "dc": "BME280", "sc": "meas", "vt": "f", "t": 1787601381}}` where `t` represents the live Epoch timeline.

### 2. Linux Background Infrastructure (`Raspberry Pi CM5`)
Managed by two independent, unbuffered, auto-starting **`systemd`** services to decouple Wi-Fi transactions from the Bluetooth hardware controller:

* **`mqtt_recorder.service`** (Runs `mqtt_to_ble_gateway.py`):
  Subscribes to `sensors/Feath/ambient`, decodes the payload dictionary, extracts the nested epoch string, and atomically overwrites a flat text file cache (`latest_epoch.txt`).
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
   git clone https://github.com
   cd ble_time_sync
   ```
2. **Deploy the Raspberry Pi CM5 Gateway Services:**
   * Move the scripts into `~/pi_ble_oled/` and set up your `.venv` virtual environment with `pip install bleak paho-mqtt`.
   * Copy the service configuration files into `/etc/systemd/system/`.
   * Initialize them:
     ```bash
     sudo systemctl daemon-reload
     sudo systemctl enable mqtt_recorder.service ble_watcher.service
     sudo systemctl start mqtt_recorder.service ble_watcher.service
     ```
3. **Compile and Flash the nRF54 DK Node:**
   * Open the project directory folder inside VS Code with the nRF Connect SDK extension active.
   * Verify your local configurations inside `src/secret.h`.
   * Execute build pass via toolchain environment terminal windows:
     ```powershell
     west build -b nrf54lm20dk/nrf54lm20b/cpuapp --sysbuild
     west flash
     ```

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
