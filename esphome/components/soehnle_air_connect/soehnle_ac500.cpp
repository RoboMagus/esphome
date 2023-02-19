#include "soehnle_ac500.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP32

namespace esphome {
namespace soehnle_air_connect {

static const char *const TAG = "soehnle_ac";
static const char *fan_states[] = {"low", "medium", "high", "turbo"};
static const char *timer_states[] = {"off", "2hours", "4hours", "8hours"};

// Estimated power usage LUT in Watts for Low, Medium, High, Turbo respectively
static const float fan_power_estimates[] = {4.5f, 12.5f, 26.5f, 53.0f};
static const float uvc_power_estimate = 5.0f;

uint8_t getTimerStrIdx(uint8_t value) {
  int idx = 0;
  while (value >>= 1)
    ++idx;
  return idx;
}

void Soehnle_AC500::setup() {
  if (this->connected_sensor_ != nullptr) {
    this->connected_sensor_->state = false;
  }
}

void Soehnle_AC500::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                        esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "Connected successfully!");
        if (this->connected_sensor_ != nullptr) {
          this->connected_sensor_->publish_state(true);
        }
      }
      break;
    }

    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGW(TAG, "Disconnected!");
      if (this->connected_sensor_ != nullptr) {
        this->connected_sensor_->publish_state(false);
      }
      break;
    }

    case ESP_GATTC_SEARCH_CMPL_EVT: {
      // this->read_handle_ = 0;
      // this->write_handle_ = 0;

      // Retrieve read characteristic handle:
      auto *read_chr = this->parent()->get_characteristic(service_uuid_, read_characteristic_uuid_);
      if (read_chr == nullptr) {
        ESP_LOGW(TAG, "No sensor read characteristic found at service %s char %s", service_uuid_.to_string().c_str(),
                 read_characteristic_uuid_.to_string().c_str());
      } else {
        this->read_handle_ = read_chr->handle;

        // Register notify:
        auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(),
                                                        this->parent()->get_remote_bda(), this->read_handle_);
        if (status) {
          ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
        }
      }

      auto *r3_chr = this->parent()->get_characteristic(service_uuid_, r3_characteristic_uuid_);
      if (r3_chr == nullptr) {
        ESP_LOGW(TAG, "No sensor read characteristic found at service %s char %s", service_uuid_.to_string().c_str(),
                 read_characteristic_uuid_.to_string().c_str());
      } else {
        this->r3_handle_ = r3_chr->handle;

        // Register notify:
        auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(),
                                                        this->parent()->get_remote_bda(), this->r3_handle_);
        if (status) {
          ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify R3 failed, status=%d", status);
        }
      }

      // Retrieve write characteristic handle:
      auto *write_chr = this->parent()->get_characteristic(service_uuid_, write_characteristic_uuid_);
      if (write_chr == nullptr) {
        ESP_LOGW(TAG, "No sensor write characteristic found at service %s char %s", service_uuid_.to_string().c_str(),
                 write_characteristic_uuid_.to_string().c_str());
      } else {
        this->write_handle_ = write_chr->handle;
        this->write_chr_props = write_chr->properties;

        // Send Auth frame to keep connection allive
        write_auth_command();
      }

      break;
    }

    case ESP_GATTC_READ_CHAR_EVT: {
      if (param->read.conn_id != this->parent()->get_conn_id())
        break;
      if (param->read.status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "Error reading char at handle %d, status=%d", param->read.handle, param->read.status);
        break;
      }
      if (param->read.handle == this->read_handle_) {
        this->parseLiveData(param->read.value, param->read.value_len);
      }
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.conn_id != this->parent()->get_conn_id())
        break;

      std::string value_str = format_hex_pretty(param->notify.value, param->notify.value_len);

      if (param->notify.handle == this->read_handle_) {
        ESP_LOGV(TAG, "ESP_GATTC_NOTIFY_EVT: handle=0x%x, value=0x%s", param->notify.handle, value_str.c_str());
        this->parseLiveData(param->notify.value, param->notify.value_len);
      }
      if (param->notify.handle == this->r3_handle_) {
        ESP_LOGW(TAG, "ESP_GATTC_NOTIFY_EVT: handle=0x%x, value=0x%s", param->notify.handle, value_str.c_str());
        /*

        Log.e(str, "confirm: " + Arrays.toString(bArr));
        if (bArr != null && Utility.bytesToHexString(bArr).equalsIgnoreCase(secondDataFrameResponse)) {
            Log.e(str, "confirmation: " + Arrays.toString(bArr));
            writeThirdCommand(observable, bluetoothDeviceItemPresenter);
        }

        */
        this->write_third_command();
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->node_state = esp32_ble_tracker::ClientState::ESTABLISHED;
      break;
    }

    default:
      break;
  }
}

void Soehnle_AC500::parseLiveData(uint8_t *bArr, uint16_t value_len) {
  std::string rawValue = format_hex_pretty(bArr, value_len);

  // No 0, 1, 2
  uint8_t aqi = bArr[3];
  const char *fanSpeed_str = fan_states[bArr[4]];
  const char *timer_str = timer_states[getTimerStrIdx(bArr[5])];

  bool isPowerOn = bArr[6] & 0x01;   // 7
  bool isUvcOn = bArr[6] & 0x02;     // 6
  bool isTimerOn = bArr[6] & 0x04;   // 5
  bool isBuzzerOn = bArr[6] & 0x08;  // 4

  bool isAutoOn = bArr[6] & 0x20;       // 2
  bool isNightModeOn = bArr[6] & 0x40;  // 1

  float pmValue = ((bArr[7] << 8) | bArr[8]) / 10.0f;

  float temperatureValue = ((bArr[9] << 8) | bArr[10]) / 10.0f;

  // No 11, 12

  float filterValue = (((bArr[13] << 8) | bArr[14]) / 4320.0) * 100.0;

  // Publish Sensors
  if (this->filter_sensor_ != nullptr && this->filter_sensor_->get_raw_state() != filterValue) {
    this->filter_sensor_->publish_state(filterValue);
  }
  if (this->temperature_sensor_ != nullptr && this->temperature_sensor_->get_raw_state() != temperatureValue) {
    this->temperature_sensor_->publish_state(temperatureValue);
  }
  if (this->particulate_sensor_ != nullptr && this->particulate_sensor_->get_raw_state() != pmValue) {
    this->particulate_sensor_->publish_state(pmValue);
  }
  if (this->raw_sensor_ != nullptr && this->raw_sensor_->get_raw_state() != rawValue) {
    this->raw_sensor_->publish_state(rawValue);
  }

  // Publish Switches
  if (this->power_switch_ != nullptr && this->power_switch_->state != isPowerOn) {
    this->power_switch_->publish_state(isPowerOn);
  }
  if (this->auto_switch_ != nullptr && this->auto_switch_->state != isAutoOn) {
    this->auto_switch_->publish_state(isAutoOn);
  }
  if (this->beeper_switch_ != nullptr && this->beeper_switch_->state != isBuzzerOn) {
    this->beeper_switch_->publish_state(isBuzzerOn);
  }
  if (this->uvc_switch_ != nullptr && this->uvc_switch_->state != isUvcOn) {
    this->uvc_switch_->publish_state(isUvcOn);
  }

  // Publish Selects
  if (this->fanspeed_select_ != nullptr && this->fanspeed_select_->state != fanSpeed_str) {
    this->fanspeed_select_->publish_state(fanSpeed_str);
  }
  if (this->timer_select_ != nullptr && this->timer_select_->state != timer_str) {
    this->timer_select_->publish_state(timer_str);
  }

  // Calculate estimated power draw
  if (this->power_sensor_ != nullptr) {
    float estimatedPower = 0.0f;
    if (isPowerOn) {
      estimatedPower = fan_power_estimates[bArr[4]];

      if (isUvcOn) {
        estimatedPower += uvc_power_estimate;
      }
    }

    if (this->power_sensor_->get_raw_state() != estimatedPower) {
      this->power_sensor_->publish_state(estimatedPower);
    }
  }
}

#if 0
void Soehnle_AC500::update() {
  if (this->node_state != esp32_ble_tracker::ClientState::ESTABLISHED) {
    if (!parent()->enabled) {
      ESP_LOGW(TAG, "Reconnecting to device");
      parent()->set_enabled(true);
      parent()->connect();
    } else {
      ESP_LOGW(TAG, "Connection in progress");
    }
  }
}
#endif

void Soehnle_AC500::write_auth_command() {
  ESP_LOGD(TAG, "writing Auth-Command");
  write_command_(0xAF, 0x00, 0x01, 0xB3);
}

void Soehnle_AC500::write_third_command() {
  ESP_LOGD(TAG, "writing 3rd-Command");
  write_command_(0xa2, 0x00, 0x01, 0xa6);
}
void Soehnle_AC500::set_power(bool on) {
  ESP_LOGD(TAG, "Send Power command:  %s ", on ? "on" : "off");
  write_command_(1, 0, on, 4 + on);
}
void Soehnle_AC500::set_auto(bool on) {
  ESP_LOGD(TAG, "Send Auto command:  %s ", on ? "on" : "off");
  write_command_(5, 0, on, 8 + on);
}
void Soehnle_AC500::set_beeper(bool on) {
  ESP_LOGD(TAG, "Send Beeper command:  %s ", on ? "on" : "off");
  write_command_(8, 0, on, 11 + on);
}
void Soehnle_AC500::set_UV_C(bool on) {
  ESP_LOGD(TAG, "Send UV-C command:  %s ", on ? "on" : "off");
  write_command_(3, 0, on, 6 + on);
}
void Soehnle_AC500::set_Night_Mode(bool on) {
  ESP_LOGD(TAG, "Send NightMode command:  %s ", on ? "on" : "off");
  write_command_(6, 0, on, 9 + on);
}

void Soehnle_AC500::set_timer(uint8_t hr) {
  if (hr == 0 || hr == 2 || hr == 4 || hr == 8) {
    ESP_LOGD(TAG, "Send SetTimer command:  %d ", hr);
    write_command_(4, 0, hr, 7 + hr);
  } else {
    ESP_LOGW(TAG, "Invalid SetTimer command:  %d ", hr);
  }
}

void Soehnle_AC500::set_timer_str(const std::string &hr_str) {
  for (uint8_t i = 0; i < sizeof(timer_states); i++) {
    if (hr_str.compare(timer_states[i]) == 0) {
      uint8_t hours = (i > 0) ? (1 << i) : (0);
      set_timer(hours);
      break;
    }
  }
}

// ToDo: Add auto logic disable when changing fan speeds
void Soehnle_AC500::set_fan(uint8_t speed) {
  // 0: Low
  // 1: Medium
  // 2: High
  // 3: Turbo
  if (speed < 4) {
    ESP_LOGD(TAG, "Send SetFan command:  %d ", speed);
    write_command_(2, 0, speed, 5 + speed);
    // Auto disabled: reflect in switch state:
    if (this->auto_switch_ != nullptr && this->auto_switch_->state == true) {
      this->auto_switch_->publish_state(false);
    }
  } else {
    ESP_LOGW(TAG, "Invalid SetFan command:  %d ", speed);
  }
}

void Soehnle_AC500::set_fan_str(const std::string &speed_str) {
  for (uint8_t i = 0; i < sizeof(fan_states); i++) {
    if (speed_str.compare(fan_states[i]) == 0) {
      set_fan(i);
      break;
    }
  }
}

void Soehnle_AC500::write_command_(char b1, char b2, char b3, char b4) {
  uint8_t command[] = {0xaa, 0x03, b1, b2, b3, b4, 0xee};

  ESP_LOGD(TAG, "Write Command: 0x%s", format_hex_pretty(command, 7).c_str());

  esp_gatt_write_type_t write_type;
  if (this->write_chr_props & ESP_GATT_CHAR_PROP_BIT_WRITE) {
    write_type = ESP_GATT_WRITE_TYPE_RSP;
    ESP_LOGV(TAG, "Write type: ESP_GATT_WRITE_TYPE_RSP");
  } else if (this->write_chr_props & ESP_GATT_CHAR_PROP_BIT_WRITE_NR) {
    write_type = ESP_GATT_WRITE_TYPE_NO_RSP;
    ESP_LOGV(TAG, "Write type: ESP_GATT_WRITE_TYPE_NO_RSP");
  } else {
    ESP_LOGE(TAG, "Characteristic %s does not allow writing", this->write_characteristic_uuid_.to_string().c_str());
    return;
  }

  auto status =
      esp_ble_gattc_write_char_descr(this->parent()->get_gattc_if(), this->parent()->get_conn_id(), this->write_handle_,
                                     sizeof(command), command, write_type, ESP_GATT_AUTH_REQ_NONE);
  if (status) {
    ESP_LOGW(TAG, "Error sending write request, status=%d", status);
  }
}

void Soehnle_AC500::dump_config() { LOG_SENSOR("  ", "Filter", this->filter_sensor_); }

Soehnle_AC500::Soehnle_AC500()
    : service_uuid_(esp32_ble_tracker::ESPBTUUID::from_raw(SERVICE_UUID)),
      write_characteristic_uuid_(esp32_ble_tracker::ESPBTUUID::from_raw(WRITE_CHARACTERISTIC_UUID)),
      read_characteristic_uuid_(esp32_ble_tracker::ESPBTUUID::from_raw(READ_CHARACTERISTIC_UUID)),
      r3_characteristic_uuid_(esp32_ble_tracker::ESPBTUUID::from_raw(READ3_CHARACTERISTIC_UUID)) {
  // Nothing...
}

}  // namespace soehnle_air_connect
}  // namespace esphome

#endif  // USE_ESP32
