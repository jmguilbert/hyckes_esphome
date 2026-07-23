#include "alpicool.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace alpicool {

static const char *const TAG = "alpicool";

void AlpicoolDevice::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Hyckes device...");
}

void AlpicoolDevice::dump_config() {
  ESP_LOGCONFIG(TAG, "Hyckes Fridge:");
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
        ESP_LOGI(TAG, "Connected to Hyckes fridge");
      } else {
        ESP_LOGW(TAG, "Connection failed, status=%d", param->open.status);
        this->publish_connected_(false);
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "Disconnected from Hyckes fridge");
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
  // Le Hyckes utilise des trames de 36 octets
  if (len != 36) {
    ESP_LOGW(TAG, "Unexpected response length: %d bytes (expected 36 for Hyckes)", len);
    return;
  }

  if (data[0] != 0xFE || data[1] != 0xFE) {
    ESP_LOGW(TAG, "Invalid preamble: 0x%02X 0x%02X", data[0], data[1]);
    return;
  }

  // Vérification de la commande (0x02 = Retour d'état)
  if (data[3] != 0x02) {
    return;
  }

  // Vérification du Checksum sur le 36ème octet (index 35)
  uint8_t expected_checksum = static_cast<uint8_t>(this->calculate_checksum_(data, 35));
  if (expected_checksum != data[35]) {
    ESP_LOGW(TAG, "Checksum mismatch: expected=0x%02X received=0x%02X", expected_checksum, data[35]);
    return;
  }

  // Décodage Hyckes
  bool is_on = (data[5] == 0x01);
  int8_t left_target_temp = static_cast<int8_t>(data[8]);
  int8_t left_actual_temp = static_cast<int8_t>(data[9]);
  int8_t right_target_temp = static_cast<int8_t>(data[22]);
  int8_t right_actual_temp = static_cast<int8_t>(data[23]);

  // Sauvegarde des états pour l'envoi de commandes futures
  this->last_settings_.on = is_on;
  this->last_settings_.temp_set = left_target_temp;
  this->last_right_settings_.temp_set = right_target_temp;
  
  this->has_settings_ = true;
  this->dual_zone_detected_ = true; // Hyfridge 85 est toujours dual-zone

  // Publication des capteurs - Zone 1
  if (this->power_switch_ != nullptr)
    this->power_switch_->publish_state(is_on);

  if (this->left_target_temp_sensor_ != nullptr)
    this->left_target_temp_sensor_->publish_state(left_target_temp);

  if (this->left_temp_number_ != nullptr)
    this->left_temp_number_->publish_state(left_target_temp);

  if (this->left_current_temp_sensor_ != nullptr)
    this->left_current_temp_sensor_->publish_state(left_actual_temp);

  // Publication des capteurs - Zone 2
  if (this->right_target_temp_sensor_ != nullptr)
    this->right_target_temp_sensor_->publish_state(right_target_temp);

  if (this->right_temp_number_ != nullptr)
    this->right_temp_number_->publish_state(right_target_temp);

  if (this->right_current_temp_sensor_ != nullptr)
    this->right_current_temp_sensor_->publish_state(right_actual_temp);
}
//================== Modif JMG
void AlpicoolDevice::send_status_request_() {
  // On casse la boucle d'attente en envoyant une trame de prise de contact 
  // codée en dur pour forcer le frigo à répondre (Handshake), sans vérifier has_settings_.
  
  uint8_t cmd[36] = {
    0xFE, 0xFE, 0x21, 0x01, 0x00, 0x01, 0x01, 0x00, 0x06, 0x08, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x1B, 0x40, 0x0B, 0x05, 0xF3, 0xF4,
    0xEC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x03, 0x00, 0x06, 0x00
  }
//====================== Fin modif JMG

  // Calcul du checksum sur les 35 premiers octets pour s'assurer que la trame est valide
  cmd[35] = static_cast<uint8_t>(this->calculate_checksum_(cmd, 35));

  // On envoie directement la commande
  this->send_command_(cmd, 36);
}

void AlpicoolDevice::send_set_temperature_(uint8_t cmd_code, int8_t temp) {
  // Cette fonction n'est plus utilisée directement avec l'architecture Hyckes,
  // les températures sont gérées par send_set_state_()
}
//==============
// modif JMG
//=============
void AlpicoolDevice::send_status_request_() {
  // On casse la boucle d'attente en envoyant une trame de prise de contact 
  // codée en dur pour forcer le frigo à répondre (Handshake), sans vérifier has_settings_.
  
  uint8_t cmd[36] = {
    0xFE, 0xFE, 0x21, 0x01, 0x00, 0x01, 0x01, 0x00, 0x06, 0x08, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x1B, 0x40, 0x0B, 0x05, 0xF3, 0xF4,
    0xEC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17, 0x00, 0x03, 0x00, 0x06, 0x00
  };

  // Calcul du checksum sur les 35 premiers octets pour s'assurer que la trame est valide
  cmd[35] = static_cast<uint8_t>(this->calculate_checksum_(cmd, 35));

  // On envoie directement la commande
  this->send_command_(cmd, 36);
}

  // Injection de l'état de l'alimentation (ON = 0x01 / OFF = 0x00) à l'index 5
  cmd[5] = this->last_settings_.on ? 0x01 : 0x00;

  // Injection des températures de consigne Zone 1 (index 8) et Zone 2 (index 22)
  cmd[8] = static_cast<uint8_t>(this->last_settings_.temp_set);
  cmd[22] = static_cast<uint8_t>(this->last_right_settings_.temp_set);

  // Calcul du checksum sur les 35 premiers octets
  uint16_t checksum_val = this->calculate_checksum_(cmd, 35);
  // Placement du checksum au 36ème octet (index 35)
  cmd[35] = static_cast<uint8_t>(checksum_val & 0xFF);

  // Log pour confirmer la structure avant l'envoi
  ESP_LOGI(TAG, "Sending 36-byte command frame to Hyckes");

  // Envoi de la commande avec la longueur strictement fixée à 36
  this->send_command_(cmd, 36);
}
//===================
// fin de la modif JMG
//===================
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
  // Le Hyckes utilise un checksum simple sur 1 octet (addition des valeurs)
  uint8_t sum = 0;
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
  if (!this->has_settings_) return;
  this->last_settings_.on = state;
  this->send_set_state_();
}

void AlpicoolDevice::send_eco(bool state) {
  if (!this->has_settings_) return;
  this->last_settings_.eco_mode = state;
  // Pas encore mappé sur Hyckes, mais la structure est prête
  // this->send_set_state_(); 
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
