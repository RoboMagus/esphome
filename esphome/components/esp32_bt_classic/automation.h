#pragma once

#ifdef USE_ESP32

#include <utility>
#include <vector>
#include "bt_classic.h"

#include "esphome/core/automation.h"

namespace esphome {
namespace esp32_bt_classic {

class BtClassicScannerNode : public BtClassicNode {
 public:
  BtClassicScannerNode(ESP32BtClassic *bt_client) {
    bt_client->register_node(this);
    bt_client_ = bt_client;
  }

  void set_scan_delay(uint32_t delay) { this->scan_delay_ = delay; }

  void scan(const BtMacAddrVector &value, uint8_t num_scans = 0) {
    for (const auto &v : value) {
      if (v.isValid()) {
        ESP_LOGV(TAG, "Adding '%02X:%02X:%02X:%02X:%02X:%02X' to scan list", EXPAND_MAC_F(v.addr));
        currentScan.push_back(bt_scan_item(v.addr, num_scans));
      } else {
        ESP_LOGE(TAG, "Invalid MAC address!! %02X:%02X:%02X:%02X:%02X:%02X", EXPAND_MAC_F(v.addr));
      }
    }

    if (!currentScan.empty()) {
      ESP_LOGD(TAG, "BtClassicScannerNode::scan()");
      parent()->startScan(currentScan.front().address);
    } else {
      ESP_LOGE(TAG, "Requested scan with empty address list!!");
    }
  }

  void loop() {
    uint32_t now = millis();
    if (!currentScan.empty() && (now + scan_delay_) > last_scan_ms) {
      parent()->startScan(currentScan.front().address);
      last_scan_ms = now;
    }
  }

  void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {}

 private:
  ESP32BtClassic *bt_client_;
  uint32_t scan_delay_{};
  uint32_t last_scan_ms{};
  std::vector<bt_scan_item> currentScan{};
};

template<typename... Ts> class BtClassicScanAction : public Action<Ts...>, public BtClassicScannerNode {
 public:
  BtClassicScanAction(ESP32BtClassic *bt_client) : BtClassicScannerNode(bt_client) {}

  void play(Ts... x) override {
    ESP_LOGI(TAG, "BtClassicScanAction::play()");
    if (has_simple_value_) {
      return scan(this->value_simple_);
    } else {
      BtMacAddrVector template_results = this->value_template_(x...);
      return scan(template_results);
    }
  }

  void set_addr_template(std::function<std::vector<std::string>(Ts...)> func) {
    ESP_LOGV(TAG, "set_addr_template()");
    this->value_template_ = std::move(func);
    has_simple_value_ = false;
  }

  void set_addr_simple(const BtMacAddrVector &addr) {
    ESP_LOGV(TAG, "set_addr_simple added %d", addr.size());
    this->value_simple_ = addr;
    has_simple_value_ = true;
  }

 private:
  bool has_simple_value_ = true;
  BtMacAddrVector value_simple_;
  std::function<std::vector<std::string>(Ts...)> value_template_{};
};

class BtClassicScanResultTrigger : public Trigger<const rmt_name_result &>, public BtClassicScanResultListner {
 public:
  explicit BtClassicScanResultTrigger(ESP32BtClassic *parent) { parent->register_listener(this); }
  void set_address(uint64_t address) { this->address_ = address; }

  bool on_scan_result(const rmt_name_result &result) override {
    // struct read_rmt_name_param {
    //   esp_bt_status_t stat;                            /*!< read Remote Name status */
    //   uint8_t rmt_name[ESP_BT_GAP_MAX_BDNAME_LEN + 1]; /*!< Remote device name */
    //   esp_bd_addr_t bda;                               /*!< remote bluetooth device address*/
    // } read_rmt_name;

    if (this->address_ && bt_mac_addr(result.bda) != bt_mac_addr(this->address_)) {
      return false;
    }
    this->trigger(result);
    return true;
  }

 protected:
  uint64_t address_ = 0;
};

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
