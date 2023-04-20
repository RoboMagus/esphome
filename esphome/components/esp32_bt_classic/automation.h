#pragma once

#ifdef USE_ESP32

#include <utility>
#include <vector>
#include "bt_classic.h"

#include "esphome/core/automation.h"

namespace esphome {
namespace esp32_bt_classic {

static const char *const TAG = "esp32_bt_automation";

class BtClassicScannerNode : public BtClassicNode {
 public:
  BtClassicScannerNode(ESP32BtClassic *bt_client) {
    bt_client->register_node(this);
    bt_client_ = bt_client;
  }

  void set_scan_delay(uint32_t delay) { this->scan_delay_ = delay; }

  void scan(const std::vector<bt_mac_addr> &value) {
    currentScan.insert(currentScan.end(), value.begin(), value.end());
    ESP_LOGD(TAG, "BtClassicScannerNode::scan()");
    parent()->startScan(currentScan.front().addr);
  }

  void loop() {
    uint32_t now = millis();
    if (!currentScan.empty() && (now + scan_delay_) > last_scan_ms) {
      parent()->startScan(currentScan.front().addr);
      last_scan_ms = now;
    }
  }

  void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {}

 private:
  ESP32BtClassic *bt_client_;
  uint32_t scan_delay_{};
  uint32_t last_scan_ms{};
  std::vector<bt_mac_addr> currentScan{};
};

template<typename... Ts> class BtClassicScanAction : public Action<Ts...>, public BtClassicScannerNode {
 public:
  BtClassicScanAction(ESP32BtClassic *bt_client) : BtClassicScannerNode(bt_client) {}

  void play(Ts... x) override {
    ESP_LOGI(TAG, "BtClassicScanAction::play()");
    if (has_simple_value_) {
      return scan(this->value_simple_);
    } else {
      return scan(this->value_template_(x...));
    }
  }

  void set_addr_template(std::function<std::vector<bt_mac_addr>(Ts...)> func) {
    ESP_LOGI(TAG, "set_addr_template()");
    this->value_template_ = std::move(func);
    has_simple_value_ = false;
  }

  void set_addr_simple(const std::vector<bt_mac_addr> &addr) {
    ESP_LOGI(TAG, "set_addr_simple added %d", addr.size());
    this->value_simple_ = addr;
    has_simple_value_ = true;
  }

 private:
  bool has_simple_value_ = true;
  std::vector<bt_mac_addr> value_simple_;
  std::function<std::vector<bt_mac_addr>(Ts...)> value_template_{};
};

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
