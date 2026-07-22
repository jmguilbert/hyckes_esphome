Attention projet non fonctionnel. En cours de développement.

# Hyfridge ESPHome Component

An ESPHome custom component for controlling Alpicool portable fridges via BLE (Bluetooth Low Energy). Integrates directly with Home Assistant through ESPHome's native API -- no MQTT broker required.

Supports both **single-zone** and **dual-zone** Alpicool fridges with automatic detection.

## Supported Devices

Alpicool fridges that advertise via BLE with names matching these patterns:

- `A1-*`
- `AK1-*`
- `AK2-*`
- `AK3-*`

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

Create a new directory for your ESPHome configuration:

```bash
mkdir alpicool-fridge && cd alpicool-fridge
```

Download the example config files:

```bash
curl -O https://raw.githubusercontent.com/jakub-hajek/alpicool_esphome/main/alpicool.yaml
curl -O https://raw.githubusercontent.com/jakub-hajek/alpicool_esphome/main/secrets.yaml.example
```

The component code is pulled automatically from GitHub by ESPHome during compilation -- no need to clone the full repository.

### Step 2: Find Your Fridge's MAC Address

Before configuring, you need to find your fridge's BLE MAC address. You have several options:

**Option A: Use the ESPHome BLE scanner**

Create a temporary `scanner.yaml`:

```yaml
esphome:
  name: ble-scanner

esp32:
  board: esp32dev
  framework:
    type: esp-idf

logger:
  level: DEBUG

esp32_ble_tracker:
```

Flash it and check the logs:

```bash
esphome run scanner.yaml
```

Look for devices with names like `A1-XXXXXXXXXXXX`, `AK1-*`, `AK2-*`, or `AK3-*`. The MAC address will be displayed next to the name.

**Option B: Use a phone app**

Use a BLE scanner app like [nRF Connect](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-mobile) (Android/iOS). Turn on the fridge and scan for nearby BLE devices. Look for names starting with `A1-`, `AK1-`, `AK2-`, or `AK3-`.

### Step 3: Create Your Secrets File

Copy the example secrets file and fill in your values:

```bash
cp secrets.yaml.example secrets.yaml
```

Edit `secrets.yaml`:

```yaml
wifi_ssid: "YourWiFiName"
wifi_password: "YourWiFiPassword"
api_key: "your-generated-api-key"
ota_password: "your-ota-password"
ap_password: "fallback-hotspot-password"
```

To generate a random API encryption key:

```bash
python3 -c "import secrets, base64; print(base64.b64encode(secrets.token_bytes(32)).decode())"
```

### Step 4: Configure the MAC Address

Edit `alpicool.yaml` and replace the MAC address placeholder:

```yaml
substitutions:
  name: alpicool-fridge
  friendly_name: "Alpicool Fridge"
  fridge_mac: "AA:BB:CC:DD:EE:FF"  # <-- your fridge's MAC address
```

### Step 5: Flash the ESP32

**First-time flash (USB cable required):**

```bash
esphome run alpicool.yaml
```

This will compile the firmware, and then prompt you to select the serial port. Connect the ESP32 via USB and select the appropriate port (e.g., `/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-*` on macOS, `COM3` on Windows).

**Subsequent updates (over-the-air):**

After the first flash, the device will be available on your network. ESPHome will automatically offer OTA as an upload option:

```bash
esphome run alpicool.yaml
# Select the network option (e.g., "alpicool-fridge.local")
```

### Step 6: Add to Home Assistant

1. Go to **Settings > Devices & Services** in Home Assistant.
2. ESPHome should auto-discover the new device. If not, click **Add Integration**, select **ESPHome**, and enter the device's IP address or hostname (`alpicool-fridge.local`).
3. Enter the API encryption key from your `secrets.yaml` when prompted.
4. All entities will appear automatically.

## Configuration

### Minimal Configuration (Single-Zone)

If you only have a single-zone fridge and want a minimal config:

```yaml
substitutions:
  name: alpicool-fridge
  friendly_name: "Alpicool Fridge"
  fridge_mac: "AA:BB:CC:DD:EE:FF"

esphome:
  name: ${name}
  friendly_name: ${friendly_name}

esp32:
  board: esp32dev
  framework:
    type: esp-idf

logger:

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password
  ap:
    ssid: "${name} Fallback"
    password: !secret ap_password

captive_portal:

external_components:
  - source:
      type: git
      url: https://github.com/jakub-hajek/alpicool_esphome
      ref: main
    components: [alpicool]

esp32_ble_tracker:

ble_client:
  - mac_address: ${fridge_mac}
    id: alpicool_ble

alpicool:
  id: alpicool_device
  ble_client_id: alpicool_ble
  update_interval: 2s

sensor:
  - platform: alpicool
    alpicool_id: alpicool_device
    current_temperature:
      name: "Temperature"
    input_voltage:
      name: "Voltage"

switch:
  - platform: alpicool
    alpicool_id: alpicool_device
    power:
      name: "Power"

number:
  - platform: alpicool
    alpicool_id: alpicool_device
    target_temperature:
      name: "Set Temperature"
```

### Full Configuration (Dual-Zone)

The default `alpicool.yaml` included in this repository exposes all available entities for both single-zone and dual-zone fridges. See the file for the complete configuration.

### Optional MQTT Publishing

The YAML includes a commented-out MQTT section. To enable MQTT publishing alongside (or instead of) the native Home Assistant API:

1. Add MQTT credentials to `secrets.yaml`:

```yaml
mqtt_broker: "192.168.1.100"
mqtt_username: "mqtt_user"
mqtt_password: "mqtt_password"
```

2. Uncomment the `mqtt:` block in `alpicool.yaml`.

ESPHome's MQTT integration automatically publishes all entity states. With the default `topic_prefix: ${mqtt_topic}` (`alpicool`), you'll see:

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

**Example: controlling the fridge from the command line:**

```bash
# Turn on
mosquitto_pub -h 192.168.1.100 -t "alpicool/cmd/power" -m "ON"

# Set temperature to -5°C
mosquitto_pub -h 192.168.1.100 -t "alpicool/cmd/set_temperature" -m "-5"

# Set right zone to 2°C (dual-zone)
mosquitto_pub -h 192.168.1.100 -t "alpicool/cmd/set_right_temperature" -m "2"

# Subscribe to all sensor data
mosquitto_sub -h 192.168.1.100 -t "alpicool/#" -v
```

> **Note:** The native API (`api:`) and MQTT can run simultaneously. If you want MQTT-only (no native API), comment out the `api:` section. However, OTA updates through the ESPHome dashboard require the native API.

### Customizing Entity Names

All entities support standard ESPHome configuration options:

```yaml
sensor:
  - platform: alpicool
    alpicool_id: alpicool_device
    current_temperature:
      name: "Fridge Temp"
      id: fridge_temp
      filters:
        - sliding_window_moving_average:
            window_size: 5
            send_every: 3
    input_voltage:
      name: "Battery Voltage"
      on_value_range:
        - below: 11.5
          then:
            - logger.log: "Low battery voltage!"
```

### Adjusting Poll Interval

The default poll interval is 2 seconds (matching the original firmware). You can adjust this:

```yaml
alpicool:
  id: alpicool_device
  ble_client_id: alpicool_ble
  update_interval: 5s  # poll every 5 seconds
```

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
├── alpicool.yaml              # Example ESPHome device configuration
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
