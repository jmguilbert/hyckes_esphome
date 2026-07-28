# Hyckes Hyfridge ESPHome Component

An ESPHome custom component for controlling Hyfridge fridges for camper via BLE (Bluetooth Low Energy). Integrates directly with Home Assistant through ESPHome's native API -- no MQTT broker required.

Supports both **single-zone** and **dual-zone** Alpicool fridges with automatic detection.

## Supported Devices

Hyckes fridges that advertise via BLE with names matching these patterns:

- `A1-*`

Both single-zone (e.g., K25) and dual-zone models are supported. The component auto-detects the fridge type from the BLE response -- no manual configuration needed.

## Exposed Entities

### All Fridges (Single-Zone and Dual-Zone)

| Entity | Type | Description |
|--------|------|-------------|
| Current Temperature | Sensor | Actual temperature inside the fridge (°C) |
| Target Temperature | Sensor | Current temperature setpoint (°C, read-only) |
| Input Voltage | Sensor | Power supply voltage (V) |
| Connected | Binary Sensor | BLE connection status |
| Power | Switch | Turn the fridge on/off |
| Eco Mode | Switch | Toggle eco (low-power) mode |
| Set Temperature | Number | Adjust target temperature (-20 to +20°C) |

### Dual-Zone Only

| Entity | Type | Description |
|--------|------|-------------|
| Right Current Temperature | Sensor | Actual temperature in the right zone (°C) |
| Right Target Temperature | Sensor | Current setpoint for the right zone (°C, read-only) |
| Set Right Temperature | Number | Adjust right zone target temperature (-20 to +20°C) |
| Compressor Running | Binary Sensor | Whether the compressor is currently active |

Dual-zone entities will simply not receive updates if your fridge is single-zone.

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
In Esphome builder, create a new project.
Copy the .yaml file.
Adapt the MAC adress of your Hyckes fridge.


**Published topics (automatic):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `alpicool/sensor/current_temperature/state` | `5` | Current temperature (°C) |
| `alpicool/sensor/target_temperature/state` | `-2` | Target temperature (°C) |
| `alpicool/sensor/input_voltage/state` | `12.4` | Input voltage (V) |
| `alpicool/sensor/right_current_temperature/state` | `3` | Right zone temp (dual-zone) |
| `alpicool/sensor/right_target_temperature/state` | `0` | Right zone target (dual-zone) |
| `alpicool/binary_sensor/connected/state` | `ON` | BLE connection status |
| `alpicool/binary_sensor/compressor_running/state` | `ON` | Compressor status (dual-zone) |
| `alpicool/switch/power/state` | `ON` | Power state |
| `alpicool/switch/eco_mode/state` | `OFF` | Eco mode state |
| `alpicool/number/set_temperature/state` | `-2` | Current setpoint |
| `alpicool/number/set_right_temperature/state` | `0` | Right zone setpoint (dual-zone) |
| `alpicool/status` | `online` | Device availability (LWT) |

**Command topics (from the `on_message` handlers):**

| Topic | Payload | Description |
|-------|---------|-------------|
| `alpicool/cmd/power` | `ON` / `OFF` | Turn fridge on or off |
| `alpicool/cmd/eco` | `ON` / `OFF` | Toggle eco mode |
| `alpicool/cmd/set_temperature` | `-5` | Set left/single-zone target (°C) |
| `alpicool/cmd/set_right_temperature` | `2` | Set right zone target (°C, dual-zone) |


### Changing the ESP32 Board

If you're using a different ESP32 board, change the `board` setting:

```yaml
esp32:
  board: nodemcu-32s    # or: esp32-c3-devkitm-1, esp-wrover-kit, etc.
  framework:
    type: esp-idf
```

## Project Structure

### Repository (GitHub)

```
alpicool_esphome/
├── hyckes.yaml              # Example ESPHome device configuration
├── secrets.yaml.example       # Template for WiFi/API credentials
├── README.md
└── components/
    └── alpicool/              # Custom component (fetched automatically by ESPHome)
        ├── __init__.py        # Component hub (BLE client node registration)
        ├── sensor.py          # Sensor platform (temperatures, voltage)
        ├── binary_sensor.py   # Binary sensor platform (connected, running)
        ├── switch.py          # Switch platform (power, eco mode)
        ├── number.py          # Number platform (target temperature controls)
        ├── alpicool.h         # C++ header (protocol structs, class definition)
        └── alpicool.cpp       # C++ implementation (BLE communication, parsing)
```

### Your Local Setup

You only need two files locally. The component is downloaded from GitHub automatically during compilation:

```
your-project/
├── alpicool.yaml              # Your device configuration
└── secrets.yaml               # Your credentials (not committed)
```

## How It Works

1. The ESP32 connects to the fridge via BLE using the configured MAC address.
2. Every 2 seconds, it sends a status request command (`0x01`) over BLE characteristic `0x1235`.
3. The fridge responds via BLE notifications on characteristic `0x1236` with a status packet containing all sensor data and settings.
4. The component parses the response and publishes values to Home Assistant via ESPHome's native API.
5. When you change a setting (power, eco, temperature) in Home Assistant, the corresponding BLE command is sent to the fridge.

### Dual-Zone Detection

The component automatically detects dual-zone fridges based on the status response length:
- **Single-zone**: 24-byte response
- **Dual-zone**: 32+ byte response (contains additional right-zone data)

No configuration flag is needed. The first status response determines the fridge type, and a log message confirms detection:

```
[I][alpicool:] Dual-zone fridge detected (response length: 37 bytes)
```

### BLE Protocol

| Service/Characteristic | UUID |
|------------------------|------|
| Service | `00001234-0000-1000-8000-00805f9b34fb` |
| Write | `00001235-0000-1000-8000-00805f9b34fb` |
| Notify | `00001236-0000-1000-8000-00805f9b34fb` |

Commands:
- `0x01` -- Status request/response
- `0x02` -- Set full state (power, eco, all settings)
- `0x05` -- Set left/single-zone target temperature
- `0x06` -- Set right zone target temperature (dual-zone only)

## Troubleshooting

### The ESP32 won't connect to the fridge

- **Check the MAC address**: Ensure it matches exactly. Use the BLE scanner to verify.
- **Distance**: Move the ESP32 closer to the fridge. BLE range is typically 5-10 meters.
- **Fridge power**: The fridge must be powered on (even if the cooling is off) for BLE to work.
- **Other connections**: If a phone app (e.g., the Alpicool app) is connected to the fridge, disconnect it first. Most Alpicool fridges only allow one BLE connection at a time.
- **Reboot**: Power cycle both the ESP32 and the fridge.

### Entities show "Unknown" or "Unavailable"

- Check the ESPHome logs for connection status messages.
- The entities update only after the first successful status response. Wait a few seconds after connection.
- If the `Connected` binary sensor shows `off`, the BLE connection is not established -- see above.

### Dual-zone entities don't update

- Dual-zone entities only update if your fridge is actually a dual-zone model. On single-zone fridges, these entities will remain in an unknown state.
- Check the logs for the "Dual-zone fridge detected" message.

### WiFi disconnects or is unstable

BLE and WiFi share the same radio on the ESP32. The `esp-idf` framework handles coexistence better than Arduino. If you experience issues:

- Reduce the poll interval (e.g., `update_interval: 5s`).
- Move the ESP32 closer to your WiFi access point.
- Use the `esp-idf` framework (already the default in this configuration).

### Compile errors

- Ensure you're using ESPHome 2024.2.0 or newer.
- The `esp-idf` framework is required. Arduino framework is not supported.
- Verify the `external_components` section in your YAML points to the correct GitHub URL and branch.

## Acknowledgments

- [jakub-hajek/alpicool-esp32-mqtt](https://github.com/jakub-hajek/alpicool-esp32-mqtt) -- Original ESP32 MQTT implementation (single-zone protocol reference)
- [johnelliott/alpicoold](https://github.com/johnelliott/alpicoold) -- Go implementation with protocol analysis
- [Gruni22/alpicool_ha_ble](https://github.com/Gruni22/alpicool_ha_ble) -- Python Home Assistant BLE integration (dual-zone protocol reference)
- [Hazelmeow/AlpicoolFridgeMonitor](https://github.com/Hazelmeow/AlpicoolFridgeMonitor) -- Python BLE monitor (dual-zone struct layouts)

## License

MIT
