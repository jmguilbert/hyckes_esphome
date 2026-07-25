#include "alpicool.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP32

namespace esphome {
namespace alpicool {

static const char *const TAG = "alpicool";

static uint8_t last_fridge_state[36] = {0};
static bool state_received = false;

// Mémorisation des canaux (ports) Bluetooth
static uint16_t handle_1235 = 0;

void AlpicoolDevice::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Hyckes device (31-BYTE CHUNKED PROTOCOL)...");
}

void AlpicoolDevice::dump_config() {
  ESP_LOGCONFIG(TAG, "Hyckes Fridge:");
  ESP_LOGCONFIG(TAG, "  Dual-zone: %s", this->dual_zone_detected_ ? "YES" : "not yet detected");
  LOG_UPDATE_INTERVAL(this);
}

void AlpicoolDevice::gattc_event_handler(esp_gattc_cb_event_t event,
                                          esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "[BLE] Connected to Hyckes fridge");
      } else {
        ESP_LOGW(TAG, "[BLE] Connection failed, status=%d", param->open.status);
        this->publish_connected_(false);
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "[BLE] Disconnected from Hyckes fridge");
      this->notify_handle_ = 0;
      handle_1235 = 0;
      this->has_settings_ = false;
      state_received = false;
      this->publish_connected_(false);
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      this->write_char_uuid_ = espbt::ESPBTUUID::from_uint16(0x1235);
      this->notify_char_uuid_ = espbt::ESPBTUUID::from_uint16(0x1236);

      auto *write_chr = this->parent()->get_characteristic(this->service_uuid_, this->write_char_uuid_);
      if (write_chr != nullptr) {
        handle_1235 = write_chr->handle;
      } else {
        this->write_char_uuid_ = espbt::ESPBTUUID::from_uint16(0x1237);
        write_chr = this->parent()->get_characteristic(this->service_uuid_, this->write_char_uuid_);
      }

      if (write_chr != nullptr) this->write_handle_ = write_chr->handle;

      auto *notify_chr = this->parent()->get_characteristic(this->service_uuid_, this->notify_char_uuid_);
      if (notify_chr != nullptr) this->notify_handle_ = notify_chr->handle;

      if (this->notify_handle_ != 0) {
        esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), this->notify_handle_);
      }
      break;
    }

    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[BLE] Notification registration failed");
        break;
      }
      this->node_state = espbt::ClientState::ESTABLISHED;
      this->publish_connected_(true);
      ESP_LOGI(TAG, "[BLE] Ready - notifications registered.");
      this->send_status_request_();
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->notify_handle_) break;
      this->parse_status_response_(param->notify.value, param->notify.value_len);
      break;
    }

    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[BLE] Write failed, status=%d", param->write.status);
      } else {
        ESP_LOGD(TAG, "[BLE] Write successful!");
      }
      break;
    }

    default:
      break;
  }
}

void AlpicoolDevice::update() {
  if (this->node_state != espbt::ClientState::ESTABLISHED) return;
  this->send_status_request_();
}

void AlpicoolDevice::parse_status_response_(const uint8_t *data, uint16_t len) {
  if (len < 36) return;

  uint16_t expected_checksum = this->calculate_checksum_(data, 34);
  uint16_t received_checksum = (data[34] << 8) | data[35];

  if (expected_checksum != received_checksum) return;

  if (data[3] == 0x01 || data[3] == 0x02) {
    memcpy(last_fridge_state, data, 36);
    state_received = true;

    bool is_on = (data[5] == 0x01);
    int8_t left_target_temp = static_cast<int8_t>(data[8]);
    int8_t left_actual_temp = static_cast<int8_t>(data[9]);
    
    // --- NOUVEAU : Récupération de la tension (Octets 20 et 21) ---
    float voltage = data[20] + (data[21] / 10.0);

    int8_t right_target_temp = static_cast<int8_t>(data[22]);
    int8_t right_actual_temp = static_cast<int8_t>(data[23]);

    this->last_settings_.on = is_on;
    this->last_settings_.temp_set = left_target_temp;
    this->last_right_settings_.temp_set = right_target_temp;
    
    this->has_settings_ = true;
    this->dual_zone_detected_ = true;

    // Publication des états vers Home Assistant
    if (this->power_switch_ != nullptr) this->power_switch_->publish_state(is_on);
    if (this->left_target_temp_sensor_ != nullptr) this->left_target_temp_sensor_->publish_state(left_target_temp);
    if (this->left_temp_number_ != nullptr) this->left_temp_number_->publish_state(left_target_temp);
    if (this->left_current_temp_sensor_ != nullptr) this->left_current_temp_sensor_->publish_state(left_actual_temp);
    if (this->right_target_temp_sensor_ != nullptr) this->right_target_temp_sensor_->publish_state(right_target_temp);
    if (this->right_temp_number_ != nullptr) this->right_temp_number_->publish_state(right_target_temp);
    if (this->right_current_temp_sensor_ != nullptr) this->right_current_temp_sensor_->publish_state(right_actual_temp);
    
    // --- NOUVEAU : Publication de la tension ---
    if (this->voltage_sensor_ != nullptr) {
        this->voltage_sensor_->publish_state(voltage);
    }
  }
}

void AlpicoolDevice::send_status_request_() {
  uint8_t cmd[6] = {0xFE, 0xFE, 0x03, 0x01, 0x00, 0x00};
  uint16_t checksum_val = this->calculate_checksum_(cmd, 4);
  cmd[4] = (checksum_val >> 8) & 0xFF; 
  cmd[5] = checksum_val & 0xFF;        
  this->send_command_(cmd, 6);
}

void AlpicoolDevice::send_set_state_() {
  if (!state_received) {
    ESP_LOGW(TAG, "ABORT SEND: No baseline frame cloned yet.");
    return;
  }

  uint8_t cmd[31];
  
  // 1. Copie des 18 premiers octets (00 à 17)
  memcpy(cmd, last_fridge_state, 18);
  cmd[2] = 0x1C; // Longueur : 28 octets
  cmd[3] = 0x02; // Commande d'écriture
  
  cmd[5] = this->last_settings_.on ? 0x01 : 0x00;
  cmd[8] = static_cast<uint8_t>(this->last_settings_.temp_set);

  // 2. L'astuce Hyckes : On saute les 4 octets de tension (18 à 21 de l'état)
  // et on copie les 8 octets de consigne droite (22 à 29 de l'état)
  memcpy(&cmd[18], &last_fridge_state[22], 8);
  cmd[18] = static_cast<uint8_t>(this->last_right_settings_.temp_set);

  // 3. On copie les 3 octets de fin de trame (31 à 33 de l'état)
  memcpy(&cmd[26], &last_fridge_state[31], 3);

  // 4. Calcul du checksum final sur les 29 premiers octets
  uint16_t checksum_val = this->calculate_checksum_(cmd, 29);
  cmd[29] = (checksum_val >> 8) & 0xFF;
  cmd[30] = checksum_val & 0xFF;

  ESP_LOGI(TAG, "--- SENDING 31-BYTE WRITE COMMAND (CHUNKED) ---");
  ESP_LOGI(TAG, "Full Hex: %s", format_hex_pretty(cmd, 31).c_str());

  this->send_command_(cmd, 31);
}

void AlpicoolDevice::send_command_(const uint8_t *data, uint16_t len) {
  if (this->node_state != espbt::ClientState::ESTABLISHED) return;

  uint16_t handle = (handle_1235 != 0) ? handle_1235 : this->write_handle_;

  // L'arme secrète : La Fragmentation Manuelle BLE (MTU = 20 max)
  uint16_t offset = 0;
  while (offset < len) {
    uint16_t chunk_size = (len - offset > 20) ? 20 : (len - offset);
    
    auto err = esp_ble_gattc_write_char(
        this->parent()->get_gattc_if(),
        this->parent()->get_conn_id(),
        handle,
        chunk_size,
        const_cast<uint8_t *>(data + offset),
        ESP_GATT_WRITE_TYPE_NO_RSP,
        ESP_GATT_AUTH_REQ_NONE);

    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Write failed at offset %d: %d", offset, err);
    } else {
      ESP_LOGD(TAG, "Sent chunk offset %d, size %d", offset, chunk_size);
    }
    
    offset += chunk_size;
    if (offset < len) {
      delay(15); // Petite pause vitale pour laisser la puce Bluetooth digérer le 1er paquet
    }
  }
}

uint16_t AlpicoolDevice::calculate_checksum_(const uint8_t *data, uint16_t len) {
  uint16_t sum = 0;
  for (uint16_t i = 0; i < len; i++) sum += data[i];
  return sum;
}

void AlpicoolDevice::publish_connected_(bool connected) {
  if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(connected);
}

void AlpicoolDevice::send_power(bool state) {
  if (!this->has_settings_) return;
  this->last_settings_.on = state;
  this->send_set_state_();
}

void AlpicoolDevice::send_eco(bool state) {
  if (!this->has_settings_) return;
  this->last_settings_.eco_mode = state;
}

void AlpicoolDevice::send_left_target_temperature(int8_t temp) {
  if (!this->has_settings_) return;
  this->last_settings_.temp_set = temp;
  this->send_set_state_();
}

void AlpicoolDevice::send_right_target_temperature(int8_t temp) {
  if (!this->has_settings_) return;
  this->last_right_settings_.temp_set = temp;
  this->send_set_state_();
}

void AlpicoolDevice::send_set_temperature_(uint8_t cmd_code, int8_t temp) {}

void AlpicoolPowerSwitch::write_state(bool state) {
  this->parent_->send_power(state);
}

void AlpicoolEcoSwitch::write_state(bool state) {
  this->parent_->send_eco(state);
}

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
