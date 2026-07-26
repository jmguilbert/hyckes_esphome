#pragma once

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include <esp_gattc_api.h>

namespace esphome {
namespace alpicool {

struct AlpicoolSettings {
  bool on{false};
  bool eco_mode{false};
  int8_t temp_set{0};
};

class AlpicoolPowerSwitch;
class AlpicoolEcoSwitch;
class AlpicoolTemperatureNumber;

class AlpicoolDevice : public ble_client::BLEClientNode, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void update();

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_power_switch(switch_::Switch *power_switch) { this->power_switch_ = power_switch; }
  void set_eco_switch(switch_::Switch *eco_switch) { this->eco_switch_ = eco_switch; }
  void set_left_temp_number(number::Number *left_temp_number) { this->left_temp_number_ = left_temp_number; }
  void set_right_temp_number(number::Number *right_temp_number) { this->right_temp_number_ = right_temp_number; }
  
  void set_left_current_temp_sensor(sensor::Sensor *sensor) { this->left_current_temp_sensor_ = sensor; }
  void set_left_target_temp_sensor(sensor::Sensor *sensor) { this->left_target_temp_sensor_ = sensor; }
  void set_right_current_temp_sensor(sensor::Sensor *sensor) { this->right_current_temp_sensor_ = sensor; }
  void set_right_target_temp_sensor(sensor::Sensor *sensor) { this->right_target_temp_sensor_ = sensor; }
  void set_voltage_sensor(sensor::Sensor *sensor) { this->voltage_sensor_ = sensor; }
  void set_connected_sensor(binary_sensor::BinarySensor *sensor) { this->connected_sensor_ = sensor; }
  void set_running_sensor(binary_sensor::BinarySensor *sensor) { this->running_sensor_ = sensor; }

  void send_power(bool state);
  void send_eco(bool state);
  void send_left_target_temperature(int8_t temp);
  void send_right_target_temperature(int8_t temp);
  
  // Fonctions pour la protection de la batterie
  uint8_t get_battery_protection() { return this->battery_protection_level_; }
  void send_battery_protection(uint8_t level);

 protected:
  void send_status_request_();
  void send_set_state_();
  void send_command_(const uint8_t *data, uint16_t len);
  void send_set_temperature_(uint8_t cmd_code, int8_t temp);
  void parse_status_response_(const uint8_t *data, uint16_t len);
  uint16_t calculate_checksum_(const uint8_t *data, uint16_t len);
  void publish_connected_(bool connected);

  esp32_ble_tracker::ESPBTUUID service_uuid_{esp32_ble_tracker::ESPBTUUID::from_uint16(0x1234)};
  esp32_ble_tracker::ESPBTUUID write_char_uuid_;
  esp32_ble_tracker::ESPBTUUID notify_char_uuid_;
  
  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};

  bool dual_zone_detected_{false};
  bool has_settings_{false};
  AlpicoolSettings last_settings_;
  AlpicoolSettings last_right_settings_;
  uint8_t battery_protection_level_{0};

  switch_::Switch *power_switch_{nullptr};
  switch_::Switch *eco_switch_{nullptr};
  number::Number *left_temp_number_{nullptr};
  number::Number *right_temp_number_{nullptr};
  
  sensor::Sensor *left_current_temp_sensor_{nullptr};
  sensor::Sensor *left_target_temp_sensor_{nullptr};
  sensor::Sensor *right_current_temp_sensor_{nullptr};
  sensor::Sensor *right_target_temp_sensor_{nullptr};
  sensor::Sensor *voltage_sensor_{nullptr};
  
  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  binary_sensor::BinarySensor *running_sensor_{nullptr};
};

class AlpicoolPowerSwitch : public switch_::Switch {
 public:
  void set_parent(AlpicoolDevice *parent) { this->parent_ = parent; }
 protected:
  void write_state(bool state) override;
  AlpicoolDevice *parent_;
};

class AlpicoolEcoSwitch : public switch_::Switch {
 public:
  void set_parent(AlpicoolDevice *parent) { this->parent_ = parent; }
 protected:
  void write_state(bool state) override;
  AlpicoolDevice *parent_;
};

class AlpicoolTemperatureNumber : public number::Number {
 public:
  void set_parent(AlpicoolDevice *parent) { this->parent_ = parent; }
  void set_is_right_zone(bool is_right_zone) { this->is_right_zone_ = is_right_zone; }
 protected:
  void control(float value) override;
  AlpicoolDevice *parent_;
  bool is_right_zone_{false};
};

}  // namespace alpicool
}  // namespace esphome

#endif
