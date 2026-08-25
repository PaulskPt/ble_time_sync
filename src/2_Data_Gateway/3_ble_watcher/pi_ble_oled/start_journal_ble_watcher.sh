#!/bin/bash
echo "===================================================="
echo " Launching Live Event-Driven BLE Transmission Logs  "
echo "===================================================="

# Execute the journalctl monitor command directly
sudo journalctl -u ble_watcher.service -f
