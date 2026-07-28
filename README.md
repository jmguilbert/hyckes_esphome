# Hyckes Hyfridge ESPHome Component

An ESPHome custom component for controlling Hyckes fridges for campers via BLE (Bluetooth Low Energy), based on the Alpicool protocol. Integrates directly with Home Assistant through ESPHome's native API -- no MQTT broker required.

Supports both **single-zone** and **dual-zone** Hyckes fridges (e.g., HyFridge 85) with automatic detection.

## Supported Devices

Hyckes fridges that advertise via BLE with names matching these patterns:
- `A1-*`
- Other Alpicool-based OEM variants.

Both single-zone and dual-zone models are supported. The component auto-detects the fridge type from the BLE response -- no manual configuration needed.

## Exposed Entities

### All Fridges (Single-Zone and Dual-Zone)

| Entity | Type | Description |
|--------|------|-------------|
| Current Temperature | Sensor | Actual temperature inside the main fridge zone (°C) |
| Target Temperature | Number (Proxy) | Adjust target temperature (+2.0 to +10.0°C) |
| Input Voltage | Sensor | Power supply voltage (V) |
| Battery Protection | Select | Battery cut-off level (High, Medium, Low) |
| Connected | Binary Sensor | BLE connection status |
| Power | Switch | Turn the fridge on/off |
| Eco Mode | Switch | Toggle eco mode (low-power/night mode) vs Max |

### Dual-Zone Only

| Entity | Type | Description |
|--------|------|-------------|
| Right Current Temperature | Sensor | Actual temperature in the freezer/right zone (°C) |
| Set Right Temperature | Number (Proxy) | Adjust right zone target temperature (-18.0 to -12.0°C) |
| Compressor Running | Binary Sensor | Whether the compressor is currently active |

*Note: Raw internal setpoint entities are hidden in Home Assistant by default (`internal: true`). The exposed Number entities act as proxies with defined min/max limits for a safer UI.*

## Prerequisites

- **ESP32 board** (e.g., ESP32-DevKitC, ESP-WROOM-32, NodeMCU-32S)
- **ESPHome** installed (2024.2.0 or newer recommended)
- **Home Assistant** (for native API integration)
- Your **fridge's BLE MAC address**

## Hardware Setup

1. Any ESP32 development board will work. The ESP32's built-in Bluetooth is used to communicate with the fridge -- no additional hardware is needed.
2. Power the ESP32 via USB or a 5V supply. Keep it within BLE range of the fridge (typically 5-10 meters, less through walls).
3. No wiring to the fridge is required. Communication is entirely wireless via BLE.

## Installation

### Step 1: Create Your Project
In the ESPHome dashboard, create a new project.
Copy the provided `.yaml` file.
Adapt the MAC address of your Hyckes fridge in the `substitutions` block.

**Published topics (Optional MQTT):**
*(MQTT can be enabled by uncommenting the MQTT block in the YAML file. The native API will continue to work alongside it).*

| Topic | Payload | Description |
|-------|---------|-------------|
| `alpicool/sensor/current_temperature/state` | `5` | Current temperature (°C) |
| `alpicool/sensor/input_voltage/state` | `12.4` | Input voltage (V) |
| `alpicool/sensor/right_current_temperature/state` | `-15` | Right zone temp (dual-zone) |
| `alpicool/binary_sensor/connected/state` | `ON` | BLE connection status |
| `alpicool/binary_sensor/compressor_running/state` | `ON` | Compressor status (dual-zone) |
| `alpicool/switch/power/state` | `ON` | Power state |
| `alpicool/switch/eco_mode/state` | `OFF` | Eco mode state |

### Changing the ESP32 Board

If you're using a different ESP32 board, change the `board` setting:

```yaml
esp32:
  board: nodemcu-32s    # or: esp32-c3-devkitm-1, esp-wrover-kit, etc.
  framework:
    type: esp-idf
```
### Project Structure
```text
hyckes_esphome/
├── hyckesv3.yaml              # ESPHome device configuration
├── README.md
└── components/
    └── alpicool/              # Custom component (fetched automatically)
        ├── __init__.py        # Component hub
        ├── sensor.py          # Sensor platform
        ├── binary_sensor.py   # Binary sensor platform
        ├── switch.py          # Switch platform
        ├── number.py          # Number platform
        ├── select.py          # Select platform (Battery protection)
        ├── alpicool.h         # C++ header (esp32_ble_tracker framework)
        └── alpicool.cpp       # C++ implementation (Chunked payload logic)
```
### Service/Characteristic,UUID
| rôle | UUID |
|---------|----------------------------------------|
| Service | 00001234-0000-1000-8000-00805f9b34fb |
| Write | 00001235-0000-1000-8000-00805f9b34fb |
| Notify | 00001236-0000-1000-8000-00805f9b34fb |

## Acknowledgments

- [jakub-hajek/alpicool-esp32-mqtt](https://github.com/jakub-hajek/alpicool-esp32-mqtt) -- Original ESP32 MQTT implementation (single-zone protocol reference)
- [johnelliott/alpicoold](https://github.com/johnelliott/alpicoold) -- Go implementation with protocol analysis
- [Gruni22/alpicool_ha_ble](https://github.com/Gruni22/alpicool_ha_ble) -- Python Home Assistant BLE integration (dual-zone protocol reference)
- [Hazelmeow/AlpicoolFridgeMonitor](https://github.com/Hazelmeow/AlpicoolFridgeMonitor) -- Python BLE monitor (dual-zone struct layouts)
- And the super help of Gemini

## License
MIT
