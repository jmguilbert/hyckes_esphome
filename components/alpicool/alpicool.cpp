#include "alpicool.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP32

namespace esphome {
namespace alpicool {

static const char *const TAG = "alpicool";

static uint8_t last_fridge_state[36] = {0};
static bool state_received = false;
static bool is_paired = false; 

// Mémorisation des canaux (ports) Bluetooth
static uint16_t handle_1235 = 0;
static uint16_t handle_1237 = 0;

void AlpicoolDevice::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Hyckes device (DUAL-CHANNEL ROUTING)...");
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
      handle_1237 = 0;
      this->has_settings_ = false;
      state_received = false;
      is_paired = false; 
      this->publish_connected_(false);
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // Recherche du canal de commande standard (1235)
      auto *char_1235 = this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(0x1235));
      if (char_1235 != nullptr) {
        handle_1235 = char_1235->handle;
        ESP_LOGI(TAG, "[BLE] Found Command Port (1235)");
      }

      // Recherche du canal de sécurité / appairage (1237)
      auto *char_1237 = this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(0x1237));
      if (char_1237 != nullptr) {
        handle_1237 = char_1237->handle;
        ESP_LOGI(TAG, "[BLE] Found Security Port (1237)");
      }

      // Recherche du canal de notification (1236)
      auto *notify_chr = this->parent()->get_characteristic(this->service_uuid_, espbt::ESPBTUUID::from_uint16(0x1236));
      if (notify_chr != nullptr) {
        this->notify_handle_ = notify_chr->handle;
        ESP_LOGI(TAG, "[BLE] Found Notify Port (1236)");
      }

      // On s'abonne aux notifications sur 1236 ET 1237 (pour capter la validation APP peu importe le port)
      if (this->notify_handle_ != 0) {
        esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), this->notify_handle_);
      }
      if (handle_1237 != 0) {
        esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(), handle_1237);
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
      break;
    }

    case ESP_GATTC_NOTIFY_EVT: {
      // On accepte les notifications de 1236 et de 1237
      if (param->notify.handle == this->notify_handle_ || param->notify.handle == handle_1237) {
        this->parse_status_response_(param->notify.value, param->notify.value_len);
      }
      break;
    }

    case ESP_GATTC_WRITE_CHAR_EVT: {
      if (param->write.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "[BLE] Write failed, status=%d", param->write.status);
      }
      break;
    }

    default:
      break;
  }
}

void AlpicoolDevice::update() {
  if (this->node_state != espbt::ClientState::ESTABLISHED) return;
  
  // Si on n'est pas appairé, on tente le Toc-Toc magique sur le port de sécurité 1237
  if (!is_paired) {
    static uint32_t last_bind_time = 0;
    uint32_t now = millis();
    // Relance toutes les 5 secondes pour ne pas spammer
    if (now - last_bind_time > 5000) {
      last_bind_time = now;
      ESP_LOGI(TAG, ">>> SENDING BIND REQUEST TO 1237: PRESS GEAR BUTTON IF 'APP' FLASHES <<<");
      uint8_t bind_cmd[6] = {0xFE, 0xFE, 0x03, 0x00, 0x00, 0x00};
      uint16_t bind_chk = this->calculate_checksum_(bind_cmd, 4);
      bind_cmd[4] = (bind_chk >> 8) & 0xFF;
      bind_cmd[5] = bind_chk & 0xFF;

      if (handle_1237 != 0) {
        esp_ble_gattc_write_char(
          this->parent()->get_gattc_if(),
          this->parent()->get_conn_id(),
          handle_1237,
          6,
          bind_cmd,
          ESP_GATT_WRITE_TYPE_NO_RSP,
          ESP_GATT_AUTH_REQ_NONE);
      }
    }
  } else {
    // Si on est appairé, on lit l'état normalement
    this->send_status_request_();
  }
}

void AlpicoolDevice::parse_status_response_(const uint8_t *data, uint16_t len) {
  if (len < 6) return;

  uint16_t expected_checksum = this->calculate_checksum_(data, len - 2);
  uint16_t received_checksum = (data[len - 2] << 8) | data[len - 1];

  if (expected_checksum != received_checksum) return;

  // 1. VALIDATION DE L'APPAIRAGE (Le frigo répond 0x00 après l'appui sur l'engrenage)
  if (data[3] == 0x00) {
    ESP_LOGI(TAG, "===============================================");
    ESP_LOGI(TAG, "!!! SESSION DÉVERROUILLÉE PAR LE FRIGO !!!");
    ESP_LOGI(TAG, "===============================================");
    is_paired = true;
    this->send_status_request_(); // On lance la lecture des capteurs
    return;
  }

  // 2. RÉPONSE D'ÉTAT (36 octets)
  if (len == 36 && (data[3] == 0x01 || data[3] == 0x02)) {
    if (!is_paired) {
        ESP_LOGI(TAG, "Data received without explicit BIND. Auto-pairing successful.");
        is_paired = true;
    }

    memcpy(last_fridge_state, data, 36);
    state_received = true;

    bool is_on = (data[5] == 0x01);
    int8_t left_target_temp = static_cast<int8_t>(data[8]);
    int8_t left_actual_temp = static_cast<int8_t>(data[9]);
    int8_t right_target_temp = static_cast<int8_t>(data[22]);
    int8_t right_actual_temp = static_cast<int8_t>(data[23]);

    this->last_settings_.on = is_on;
    this->last_settings_.temp_set = left_target_temp;
    this->last_right_settings_.temp_set = right_target_temp;
    
    this->has_settings_ = true;
    this->dual_zone_detected_ = true;

    if (this->power_switch_ != nullptr) this->power_switch_->publish_state(is_on);
    if (this->left_target_temp_sensor_ != nullptr) this->left_target_temp_sensor_->publish_state(left_target_temp);
    if (this->left_temp_number_ != nullptr) this->left_temp_number_->publish_state(left_target_temp);
    if (this->left_current_temp_sensor_ != nullptr) this->left_current_temp_sensor_->publish_state(left_actual_temp);
    if (this->right_target_temp_sensor_ != nullptr) this->right_target_temp_sensor_->publish_state(right_target_temp);
    if (this->right_temp_number_ != nullptr) this->right_temp_number_->publish_state(right_target_temp);
    if (this->right_current_temp_sensor_ != nullptr) this->right_current_temp_sensor_->publish_state(right_actual_temp);
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
  if (!is_paired) {
    ESP_LOGW(TAG, "ABORT SEND: Session is locked (waiting for Login Auth).");
    return;
  }
  if (!state_received) {
    ESP_LOGW(TAG, "ABORT SEND: No valid baseline frame cloned from fridge yet.");
    return;
  }

  uint8_t cmd[36];
  memcpy(cmd, last_fridge_state, 36);

  cmd[3] = 0x02; // Commande WRITE
  
  cmd[5] = this->last_settings_.on ? 0x01 : 0x00;
  cmd[8] = static_cast<uint8_t>(this->last_settings_.temp_set);
  cmd[22] = static_cast<uint8_t>(this->last_right_settings_.temp_set);

  uint16_t checksum_val = this->calculate_checksum_(cmd, 34);
  cmd[34] = (checksum_val >> 8) & 0xFF;
  cmd[35] = checksum_val & 0xFF;

  ESP_LOGI(TAG, "--- SENDING WRITE COMMAND (0x02) TO 1235 ---");
  this->send_command_(cmd, 36);
}

void AlpicoolDevice::send_command_(const uint8_t *data, uint16_t len) {
  if (this->node_state != espbt::ClientState::ESTABLISHED) return;

  // On route systématiquement les PING (0x01) et les SET (0x02) vers le port de commande 1235 !
  uint16_t handle = (handle_1235 != 0) ? handle_1235 : this->write_handle_;

  auto err = esp_ble_gattc_write_char(
      this->parent()->get_gattc_if(),
      this->parent()->get_conn_id(),
      handle,
      len,
      const_cast<uint8_t *>(data),
      ESP_GATT_WRITE_TYPE_NO_RSP,
      ESP_GATT_AUTH_REQ_NONE);

  if (err != ESP_OK) ESP_LOGW(TAG, "Write failed: %d", err);
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
