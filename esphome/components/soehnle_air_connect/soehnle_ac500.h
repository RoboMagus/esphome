#pragma once

#ifdef USE_ESP32

#include <esp_gattc_api.h>
#include <algorithm>
#include <iterator>
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace soehnle_air_connect {

static const char *const SERVICE_UUID = "FFA0";
static const char *const WRITE_CHARACTERISTIC_UUID = "0000ef01-0000-1000-8000-00805f9b34fb";
static const char *const READ_CHARACTERISTIC_UUID = "0000ef02-0000-1000-8000-00805f9b34fb";
static const char *const READ3_CHARACTERISTIC_UUID = "0000ef03-0000-1000-8000-00805f9b34fb";

class DeviceSwitch : public switch_::Switch, public Component {
 public:
  void setup() override {}
  void dump_config() override {}

  void set_write_state(std::function<void(bool)> impl) { this->write_state_impl_ = impl; }

 protected:
  void write_state(bool state) override { this->write_state_impl_(state); }

  std::function<void(bool)> write_state_impl_;
};

class DeviceSelect : public select::Select, public Component {
 public:
  void setup() override{};
  void dump_config() override{};

  void set_control(std::function<void(const std::string &)> &&impl) { this->control_impl_ = std::move(impl); }

 protected:
  void control(const std::string &value) override { this->control_impl_(value); }

  std::function<void(const std::string &)> control_impl_;
};

class Soehnle_AC500 : public Component, public ble_client::BLEClientNode {
 public:
  Soehnle_AC500();

  void dump_config() override;
  void setup() override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_connected_sensor(binary_sensor::BinarySensor *connected) { connected_sensor_ = connected; }

  void set_filter_sensor(sensor::Sensor *filter) { filter_sensor_ = filter; }
  void set_temperature_sensor(sensor::Sensor *temperature) { temperature_sensor_ = temperature; }
  void set_particulate_sensor(sensor::Sensor *particulate) { particulate_sensor_ = particulate; }
  void set_power_sensor(sensor::Sensor *power_sensor) { power_sensor_ = power_sensor; }
  void set_raw_sensor(text_sensor::TextSensor *raw) { raw_sensor_ = raw; }

  void set_power_switch(DeviceSwitch *power) {
    power_switch_ = power;
    power->set_write_state([this](bool state) { this->set_power(state); });
  }
  void set_auto_switch(DeviceSwitch *auto_switch) {
    auto_switch_ = auto_switch;
    auto_switch->set_write_state([this](bool state) { this->set_auto(state); });
  }
  void set_beeper_switch(DeviceSwitch *beeper) {
    beeper_switch_ = beeper;
    beeper->set_write_state([this](bool state) { this->set_beeper(state); });
  }
  void set_uvc_switch(DeviceSwitch *uvc) {
    uvc_switch_ = uvc;
    uvc->set_write_state([this](bool state) { this->set_UV_C(state); });
  }

  void set_fan_select(DeviceSelect *fan) {
    fanspeed_select_ = fan;
    fan->set_control([this](const std::string &value) { this->set_fan_str(value); });
  }

  void set_timer_select(DeviceSelect *timer) {
    timer_select_ = timer;
    timer->set_control([this](const std::string &value) { this->set_timer_str(value); });
  }

  void set_power(bool on);
  void set_auto(bool on);
  void set_beeper(bool on);
  void set_UV_C(bool on);
  void set_Night_Mode(bool on);

  void set_timer(uint8_t hr);
  void set_timer_str(const std::string &hr_str);
  // ToDo: Add auto logic disable when changing fan speeds
  void set_fan(uint8_t speed);
  void set_fan_str(const std::string &speed_str);

 protected:
  void write_command_(char b1, char b2, char b3, char b4);
  void write_auth_command();
  void write_third_command();

  void parseLiveData(uint8_t *bArr, uint16_t value_len);

  binary_sensor::BinarySensor *connected_sensor_{nullptr};

  sensor::Sensor *filter_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *particulate_sensor_{nullptr};
  sensor::Sensor *power_sensor_{nullptr};

  text_sensor::TextSensor *raw_sensor_{nullptr};

  DeviceSwitch *power_switch_{nullptr};
  DeviceSwitch *auto_switch_{nullptr};
  DeviceSwitch *beeper_switch_{nullptr};
  DeviceSwitch *uvc_switch_{nullptr};

  DeviceSelect *fanspeed_select_{nullptr};
  DeviceSelect *timer_select_{nullptr};

  uint16_t read_handle_;
  uint16_t r3_handle_;
  uint16_t write_handle_;

  esp_gatt_char_prop_t write_chr_props;

  esp32_ble_tracker::ESPBTUUID service_uuid_;
  esp32_ble_tracker::ESPBTUUID write_characteristic_uuid_;
  esp32_ble_tracker::ESPBTUUID read_characteristic_uuid_;
  esp32_ble_tracker::ESPBTUUID r3_characteristic_uuid_;
};

}  // namespace soehnle_air_connect
}  // namespace esphome

#endif  // USE_ESP32
