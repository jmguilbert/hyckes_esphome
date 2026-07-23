#include "alpicool.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace alpicool {

static const char *const TAG = "alpicool";

void AlpicoolDevice::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Alpicool device...");
}

void AlpicoolDevice::dump_config() {
  ESP_LOGCONFIG(TAG, "Alpicool Fridge:");
  ESP_LOGCONFIG(TAG, "  Dual-zone: %s", this->dual_zone_detected_ ? "YES" : "not yet detected");
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Left Current Temperature", this->left_current_temp_sensor_);
  LOG_SENSOR("  ", "Left Target Temperature", this->left_target_temp_sensor_);
  LOG_SENSOR("  ", "Input Voltage", this->voltage_sensor_);
  LOG_BINARY_SENSOR("  ", "Connected", this->connected_sensor_);
  LOG_BINARY_SENSOR("  ", "Running", this->running_sensor_);
  LOG_SWITCH("  ", "Power", this->power_switch_);
  LOG_SWITCH("  ", "Eco Mode", this->eco_switch_);
  LOG_NUMBER("  ", "Left Target Temperature", this->left_temp_number_);
  LOG_SENSOR("  ", "Right Current Temperature", this->right_current_temp_sensor_);
  LOG_SENSOR("  ", "Right Target Temperature", this->right_target_temp_sensor_);
  LOG_NUMBER("  ", "Right Target Temperature", this->right_temp_number_);
}

void AlpicoolDevice::gattc_event_handler(esp_gattc_cb_event_t event,
                                          esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "Connected to Alpicool fridge");
      } else {
        ESP_LOGW(TAG, "Connection failed, status=%d", param->open.status);
        this->publish_connected_(false);
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "Disconnected from Alpicool fridge");
      this->notify_handle_ = 0;
      this->write_handle_ = 0;
      this->has_settings_ = false;
      this->publish_connected_(false);
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *write_chr = this->parent()->get_characteristic(this->service_uuid_, this->write_char_uuid_);
      if (write_chr == nullptr) {
        ESP_LOGW(TAG, "Write characteristic not found");
        break;
      }
      this->write_handle_ = write_chr->handle;

      auto *notify_chr = this->parent()->get_characteristic(this->service_uuid_, this->notify_char_uuid_);
      if (notify_chr == nullptr) {
        ESP_LOGW(TAG, "Notify characteristic not found");
        break;
      }
      this->notify_handle_ = notify_chr->handle;

      auto status = esp_ble_gattc_register_for_notify(
          this->parent()->get_gattc_if(),
          this->parent()->get_remote_bda(),
          this->notify_handle_);
      if (status) {
        ESP_LOGW(TAG, "Failed to register for notifications, status=%d", status);
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Notification registration failed, status=%d", param->reg_for_notify.status);
        break;
      }
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->publish_connected_(true);
      ESP_LOGI(TAG, "Ready - notifications registered");
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->notify_handle_)
        break;
      this->parse_status_response_(param->notify.value, param->notify.value_len);
      break;
    }

    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Write failed, status=%d", param->write.status);
      }
      break;
    }

    default:
      break;
  }
}

void AlpicoolDevice::update() {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    return;
  }
  this->send_status_request_();
}

void AlpicoolDevice::parse_status_response_(const uint8_t *data, uint16_t len) {
  if (len < MIN_RESPONSE_LEN) {
    ESP_LOGW(TAG, "Short response: %d bytes (expected >= %d)", len, MIN_RESPONSE_LEN);
    return;
  }

  if (data[0] != PREAMBLE_1 || data[1] != PREAMBLE_2) {
    ESP_LOGW(TAG, "Invalid preamble: 0x%02X 0x%02X", data[0], data[1]);
    return;
  }

  if (data[3] != CMD_STATUS_REQUEST) {
    ESP_LOGD(TAG, "Not a status response (cmd=0x%02X), ignoring", data[3]);
    return;
  }

  // Verify checksum
  uint16_t expected_checksum = this->calculate_checksum_(data, len - 2);
  uint16_t received_checksum = (data[len - 2] << 8) | data[len - 1];
  if (expected_checksum != received_checksum) {
    ESP_LOGW(TAG, "Checksum mismatch: expected=0x%04X received=0x%04X", expected_checksum, received_checksum);
    return;
  }

  // Parse left zone / global settings (offset 4..17)
  AlpicoolSettings settings;
  settings.locked = data[4];
  settings.on = data[5];
  settings.eco_mode = data[6];
  settings.h_lvl = static_cast<int8_t>(data[7]);
  settings.temp_set = static_cast<int8_t>(data[8]);
  settings.highest_temp = static_cast<int8_t>(data[9]);
  settings.lowest_temp = static_cast<int8_t>(data[10]);
  settings.hysteresis = static_cast<int8_t>(data[11]);
  settings.soft_start = static_cast<int8_t>(data[12]);
  settings.celsius_mode = data[13];
  settings.temp_comp_gte_m6 = static_cast<int8_t>(data[14]);
  settings.temp_comp_gte_m12 = static_cast<int8_t>(data[15]);
  settings.temp_comp_lt_m12 = static_cast<int8_t>(data[16]);
  settings.temp_comp_shutdown = static_cast<int8_t>(data[17]);

  this->last_settings_ = settings;
  this->has_settings_ = true;

  // Parse left zone sensors (offset 18..21)
  int8_t left_actual_temp = static_cast<int8_t>(data[18]);
  // data[19] = battery percent / unknown
  int8_t voltage_whole = static_cast<int8_t>(data[20]);
  int8_t voltage_frac = static_cast<int8_t>(data[21]);
  float voltage = voltage_whole + 0.1f * voltage_frac;

  // Publish left zone / global values
  if (this->left_current_temp_sensor_ != nullptr)
    this->left_current_temp_sensor_->publish_state(left_actual_temp);

  if (this->left_target_temp_sensor_ != nullptr)
    this->left_target_temp_sensor_->publish_state(settings.temp_set);

  if (this->voltage_sensor_ != nullptr)
    this->voltage_sensor_->publish_state(voltage);

  if (this->power_switch_ != nullptr)
    this->power_switch_->publish_state(settings.on);

  if (this->eco_switch_ != nullptr)
    this->eco_switch_->publish_state(settings.eco_mode);

  if (this->left_temp_number_ != nullptr)
    this->left_temp_number_->publish_state(settings.temp_set);

  // Detect and parse dual-zone extension (offsets 22..31)
  // Dual-zone responses have at least 32 bytes before checksum
  if (len >= DUAL_ZONE_MIN_LEN) {
    if (!this->dual_zone_detected_) {
      this->dual_zone_detected_ = true;
      ESP_LOGI(TAG, "Dual-zone fridge detected (response length: %d bytes)", len);
    }

    AlpicoolRightZoneSettings right;
    right.temp_set = static_cast<int8_t>(data[22]);
    // data[23], data[24] = unknown
    right.hysteresis = static_cast<int8_t>(data[25]);
    right.temp_comp_gte_m6 = static_cast<int8_t>(data[26]);
    right.temp_comp_gte_m12 = static_cast<int8_t>(data[27]);
    right.temp_comp_lt_m12 = static_cast<int8_t>(data[28]);
    right.temp_comp_shutdown = static_cast<int8_t>(data[29]);
    int8_t right_actual_temp = static_cast<int8_t>(data[30]);

    this->last_right_settings_ = right;

    // Running status at offset 31
    bool running = data[31] != 0;

    // Publish right zone values
    if (this->right_current_temp_sensor_ != nullptr)
      this->right_current_temp_sensor_->publish_state(right_actual_temp);

    if (this->right_target_temp_sensor_ != nullptr)
      this->right_target_temp_sensor_->publish_state(right.temp_set);

    if (this->right_temp_number_ != nullptr)
      this->right_temp_number_->publish_state(right.temp_set);

    if (this->running_sensor_ != nullptr)
      this->running_sensor_->publish_state(running);
  }
}

void AlpicoolDevice::send_status_request_() {
  uint8_t cmd[] = {PREAMBLE_1, PREAMBLE_2, 0x03, CMD_STATUS_REQUEST, 0x00, 0x00};
  uint16_t checksum = this->calculate_checksum_(cmd, 4);
  cmd[4] = (checksum >> 8) & 0xFF;
  cmd[5] = checksum & 0xFF;
  this->send_command_(cmd, sizeof(cmd));
}

void AlpicoolDevice::send_set_temperature_(uint8_t cmd_code, int8_t temp) {
  uint8_t cmd[] = {PREAMBLE_1, PREAMBLE_2, 0x04, cmd_code, static_cast<uint8_t>(temp), 0x00, 0x00};
  uint16_t checksum = this->calculate_checksum_(cmd, 5);
  cmd[5] = (checksum >> 8) & 0xFF;
  cmd[6] = checksum & 0xFF;
  this->send_command_(cmd, sizeof(cmd));
}

void AlpicoolDevice::send_set_state_() {
  const auto &s = this->last_settings_;

  if (this->dual_zone_detected_) {
    // Dual-zone: 0xFE 0xFE <len> 0x02 [14 left settings] [11 right settings] [checksum]
    // Total: 2 preamble + 1 len + 1 cmd + 14 left + 11 right + 2 checksum = 31 bytes
    uint8_t cmd[31];
    cmd[0] = PREAMBLE_1;
    cmd[1] = PREAMBLE_2;
    cmd[2] = 0x1C;  // data length: 28 bytes (cmd + 14 left + 11 right + 2 checksum)
    cmd[3] = CMD_SET_STATE;

    // Left zone settings (14 bytes)
    cmd[4] = s.locked ? 1 : 0;
    cmd[5] = s.on ? 1 : 0;
    cmd[6] = s.eco_mode ? 1 : 0;
    cmd[7] = static_cast<uint8_t>(s.h_lvl);
    cmd[8] = static_cast<uint8_t>(s.temp_set);
    cmd[9] = static_cast<uint8_t>(s.highest_temp);
    cmd[10] = static_cast<uint8_t>(s.lowest_temp);
    cmd[11] = static_cast<uint8_t>(s.hysteresis);
    cmd[12] = static_cast<uint8_t>(s.soft_start);
    cmd[13] = s.celsius_mode ? 1 : 0;
    cmd[14] = static_cast<uint8_t>(s.temp_comp_gte_m6);
    cmd[15] = static_cast<uint8_t>(s.temp_comp_gte_m12);
    cmd[16] = static_cast<uint8_t>(s.temp_comp_lt_m12);
    cmd[17] = static_cast<uint8_t>(s.temp_comp_shutdown);

    // Right zone settings (11 bytes)
    const auto &r = this->last_right_settings_;
    cmd[18] = static_cast<uint8_t>(r.temp_set);
    cmd[19] = 0x00;  // unknown
    cmd[20] = 0x00;  // unknown
    cmd[21] = static_cast<uint8_t>(r.hysteresis);
    cmd[22] = static_cast<uint8_t>(r.temp_comp_gte_m6);
    cmd[23] = static_cast<uint8_t>(r.temp_comp_gte_m12);
    cmd[24] = static_cast<uint8_t>(r.temp_comp_lt_m12);
    cmd[25] = static_cast<uint8_t>(r.temp_comp_shutdown);
    cmd[26] = 0x00;  // unknown
    cmd[27] = 0x00;  // unknown
    cmd[28] = 0x00;  // unknown

    uint16_t checksum = this->calculate_checksum_(cmd, 29);
    cmd[29] = (checksum >> 8) & 0xFF;
    cmd[30] = checksum & 0xFF;
    this->send_command_(cmd, sizeof(cmd));
  } else {
    // Single-zone: 0xFE 0xFE 0x11 0x02 [14 settings] [checksum]
    uint8_t cmd[20];
    cmd[0] = PREAMBLE_1;
    cmd[1] = PREAMBLE_2;
    cmd[2] = 0x11;  // data length: 17 bytes
    cmd[3] = CMD_SET_STATE;
    cmd[4] = s.locked ? 1 : 0;
    cmd[5] = s.on ? 1 : 0;
    cmd[6] = s.eco_mode ? 1 : 0;
    cmd[7] = static_cast<uint8_t>(s.h_lvl);
    cmd[8] = static_cast<uint8_t>(s.temp_set);
    cmd[9] = static_cast<uint8_t>(s.highest_temp);
    cmd[10] = static_cast<uint8_t>(s.lowest_temp);
    cmd[11] = static_cast<uint8_t>(s.hysteresis);
    cmd[12] = static_cast<uint8_t>(s.soft_start);
    cmd[13] = s.celsius_mode ? 1 : 0;
    cmd[14] = static_cast<uint8_t>(s.temp_comp_gte_m6);
    cmd[15] = static_cast<uint8_t>(s.temp_comp_gte_m12);
    cmd[16] = static_cast<uint8_t>(s.temp_comp_lt_m12);
    cmd[17] = static_cast<uint8_t>(s.temp_comp_shutdown);

    uint16_t checksum = this->calculate_checksum_(cmd, 18);
    cmd[18] = (checksum >> 8) & 0xFF;
    cmd[19] = checksum & 0xFF;
    this->send_command_(cmd, sizeof(cmd));
  }
}

void AlpicoolDevice::send_command_(const uint8_t *data, uint16_t len) {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "Not connected, cannot send command");
    return;
  }

  auto err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(),
      this->parent()->get_conn_id(),
      this->write_handle_,
      len,
      const_cast<uint8_t *>(data),
      ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Write failed: %d", err);
  }
}

uint16_t AlpicoolDevice::calculate_checksum_(const uint8_t *data, uint16_t len) {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < len; i++) {
    sum += data[i];
  }
  return sum;
}

void AlpicoolDevice::publish_connected_(bool connected) {
  if (this->connected_sensor_ != nullptr)
    this->connected_sensor_->publish_state(connected);
}

void AlpicoolDevice::send_power(bool state) {
  if (!this->has_settings_) {
    ESP_LOGW(TAG, "No settings received yet, cannot change power state");
    return;
  }
  this->last_settings_.on = state;
  this->send_set_state_();
}

void AlpicoolDevice::send_eco(bool state) {
  if (!this->has_settings_) {
    ESP_LOGW(TAG, "No settings received yet, cannot change eco mode");
    return;
  }
  this->last_settings_.eco_mode = state;
  this->send_set_state_();
}

void AlpicoolDevice::send_left_target_temperature(int8_t temp) {
  if (this->has_settings_) {
    if (temp < this->last_settings_.lowest_temp)
      temp = this->last_settings_.lowest_temp;
    if (temp > this->last_settings_.highest_temp)
      temp = this->last_settings_.highest_temp;
  }
  this->send_set_temperature_(CMD_SET_TEMP_LEFT, temp);
}

void AlpicoolDevice::send_right_target_temperature(int8_t temp) {
  if (!this->dual_zone_detected_) {
    ESP_LOGW(TAG, "Right zone temperature set ignored - not a dual-zone fridge");
    return;
  }
  if (this->has_settings_) {
    if (temp < this->last_settings_.lowest_temp)
      temp = this->last_settings_.lowest_temp;
    if (temp > this->last_settings_.highest_temp)
      temp = this->last_settings_.highest_temp;
  }
  this->send_set_temperature_(CMD_SET_TEMP_RIGHT, temp);
}

// Switch implementations
void AlpicoolPowerSwitch::write_state(bool state) {
  this->parent_->send_power(state);
}

void AlpicoolEcoSwitch::write_state(bool state) {
  this->parent_->send_eco(state);
}

// Number implementation
void AlpicoolTemperatureNumber::control(float value) {
  int8_t temp = static_cast<int8_t>(value);
  if (this->is_right_zone_) {
    this->parent_->send_right_target_temperature(temp);
  } else {
    this->parent_->send_left_target_temperature(temp);
  }
}

}  // namespace alpicool
}  // namespace esphome

#endif
