#pragma once

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"

#ifdef USE_ESP32

#include <esp_gattc_api.h>

namespace esphome {
namespace alpicool {

namespace espbt = esphome::esp32_ble_tracker;

static const uint8_t PREAMBLE_1 = 0xFE;
static const uint8_t PREAMBLE_2 = 0xFE;
static const uint8_t CMD_STATUS_REQUEST = 0x01;
static const uint8_t CMD_SET_STATE = 0x02;
static const uint8_t CMD_SET_TEMP_LEFT = 0x05;
static const uint8_t CMD_SET_TEMP_RIGHT = 0x06;

// Minimum response length for single-zone (24 bytes)
static const uint8_t MIN_RESPONSE_LEN = 24;
// Minimum response length for dual-zone detection (32 bytes of payload data)
static const uint8_t DUAL_ZONE_MIN_LEN = 32;

struct AlpicoolSettings {
  bool locked{false};
  bool on{false};
  bool eco_mode{false};
  int8_t h_lvl{0};
  int8_t temp_set{0};
  int8_t highest_temp{20};
  int8_t lowest_temp{-20};
  int8_t hysteresis{0};
  int8_t soft_start{0};
  bool celsius_mode{true};
  int8_t temp_comp_gte_m6{0};
  int8_t temp_comp_gte_m12{0};
  int8_t temp_comp_lt_m12{0};
  int8_t temp_comp_shutdown{0};
};

struct AlpicoolRightZoneSettings {
  int8_t temp_set{0};
  int8_t hysteresis{0};
  int8_t temp_comp_gte_m6{0};
  int8_t temp_comp_gte_m12{0};
  int8_t temp_comp_lt_m12{0};
  int8_t temp_comp_shutdown{0};
};

class AlpicoolDevice;

class AlpicoolPowerSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(AlpicoolDevice *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  AlpicoolDevice *parent_{nullptr};
};

class AlpicoolEcoSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(AlpicoolDevice *parent) { this->parent_ = parent; }
  void write_state(bool state) override;

 protected:
  AlpicoolDevice *parent_{nullptr};
};

class AlpicoolTemperatureNumber : public number::Number, public Component {
 public:
  void set_parent(AlpicoolDevice *parent) { this->parent_ = parent; }
  void set_is_right_zone(bool right) { this->is_right_zone_ = right; }
  void control(float value) override;

 protected:
  AlpicoolDevice *parent_{nullptr};
  bool is_right_zone_{false};
};

class AlpicoolDevice : public PollingComponent, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void update() override;
  void dump_config() override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  // Left zone (or single-zone) sensors
  void set_current_temperature_sensor(sensor::Sensor *s) { this->left_current_temp_sensor_ = s; }
  void set_target_temperature_sensor(sensor::Sensor *s) { this->left_target_temp_sensor_ = s; }
  void set_voltage_sensor(sensor::Sensor *s) { this->voltage_sensor_ = s; }
  void set_connected_binary_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_running_binary_sensor(binary_sensor::BinarySensor *s) { this->running_sensor_ = s; }
  void set_power_switch(AlpicoolPowerSwitch *s) { this->power_switch_ = s; }
  void set_eco_switch(AlpicoolEcoSwitch *s) { this->eco_switch_ = s; }
  void set_temperature_number(AlpicoolTemperatureNumber *n) { this->left_temp_number_ = n; }

  // Right zone sensors (dual-zone only)
  void set_right_current_temperature_sensor(sensor::Sensor *s) { this->right_current_temp_sensor_ = s; }
  void set_right_target_temperature_sensor(sensor::Sensor *s) { this->right_target_temp_sensor_ = s; }
  void set_right_temperature_number(AlpicoolTemperatureNumber *n) { this->right_temp_number_ = n; }

  void send_power(bool state);
  void send_eco(bool state);
  void send_left_target_temperature(int8_t temp);
  void send_right_target_temperature(int8_t temp);

  bool is_dual_zone() const { return this->dual_zone_detected_; }

  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

 protected:
  void send_status_request_();
  void send_set_state_();
  void send_set_temperature_(uint8_t cmd, int8_t temp);
  void send_command_(const uint8_t *data, uint16_t len);
  void parse_status_response_(const uint8_t *data, uint16_t len);
  uint16_t calculate_checksum_(const uint8_t *data, uint16_t len);
  void publish_connected_(bool connected);

  // Left zone (or single-zone) sensors
  sensor::Sensor *left_current_temp_sensor_{nullptr};
  sensor::Sensor *left_target_temp_sensor_{nullptr};
  sensor::Sensor *voltage_sensor_{nullptr};
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *running_sensor_{nullptr};
  AlpicoolPowerSwitch *power_switch_{nullptr};
  AlpicoolEcoSwitch *eco_switch_{nullptr};
  AlpicoolTemperatureNumber *left_temp_number_{nullptr};

  // Right zone sensors (dual-zone only)
  sensor::Sensor *right_current_temp_sensor_{nullptr};
  sensor::Sensor *right_target_temp_sensor_{nullptr};
  AlpicoolTemperatureNumber *right_temp_number_{nullptr};

  uint16_t notify_handle_{0};
  uint16_t write_handle_{0};

  AlpicoolSettings last_settings_{};
  AlpicoolRightZoneSettings last_right_settings_{};
  bool has_settings_{false};
  bool dual_zone_detected_{false};

  espbt::ESPBTUUID service_uuid_ =
      espbt::ESPBTUUID::from_raw("00001234-0000-1000-8000-00805f9b34fb");
  espbt::ESPBTUUID write_char_uuid_ =
      espbt::ESPBTUUID::from_raw("00001235-0000-1000-8000-00805f9b34fb");
  espbt::ESPBTUUID notify_char_uuid_ =
      espbt::ESPBTUUID::from_raw("00001236-0000-1000-8000-00805f9b34fb");
};

}  // namespace alpicool
}  // namespace esphome

#endif
