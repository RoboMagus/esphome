#pragma once

#ifdef USE_ESP32

#include <utility>
#include <vector>
#include "bt_classic.h"

#include "esphome/core/automation.h"

namespace esphome {
namespace esp32_bt_classic {

template<typename T> void moveItemToBack(std::vector<T> &v, size_t itemIndex) {
  T tmp(v[itemIndex]);
  v.erase(v.begin() + itemIndex);
  v.push_back(tmp);
}

class BtClassicScannerNode : public BtClassicNode {
 public:
  BtClassicScannerNode(ESP32BtClassic *bt_client) {
    bt_client->register_node(this);
    bt_client_ = bt_client;
  }

  void set_scan_delay(uint32_t delay) { this->scan_delay_ = delay; }

  void scan(const BtMacAddrVector &value, uint16_t num_scans) {
    for (const auto &v : value) {
      if (v.isValid()) {
        ESP_LOGV(TAG, "Adding '%02X:%02X:%02X:%02X:%02X:%02X' to scan list with %d scans", EXPAND_MAC_F(v.addr),
                 num_scans);
        currentScan.push_back(bt_scan_item(v.addr, num_scans));
      } else {
        ESP_LOGE(TAG, "Invalid MAC address!! %02X:%02X:%02X:%02X:%02X:%02X", EXPAND_MAC_F(v.addr));
      }
    }
  }

  void loop() {
    uint32_t now = millis();
    if (!currentScan.empty() && (now + scan_delay_) > last_scan_ms && !scanPending_) {
      if (currentScan.front().scans_remaining > 0) {
        parent()->startScan(currentScan.front().address);
        currentScan.front().scans_remaining--;
        scanPending_ = true;
      } else {
        currentScan.erase(currentScan.begin());
      }
      last_scan_ms = now;
    }
  }

  void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
    if (event == ESP_BT_GAP_READ_REMOTE_NAME_EVT) {
      auto it = currentScan.begin();
      while (it != currentScan.end()) {
        if (0 == memcmp(it->address, param->read_rmt_name.bda, sizeof(esp_bd_addr_t))) {
          // If device was found, remove it from the scan list
          if (ESP_BT_STATUS_SUCCESS == param->read_rmt_name.stat) {
            ESP_LOGI(TAG, "Found device '%02X:%02X:%02X:%02X:%02X:%02X' (%s) with %d scans remaining",
                     EXPAND_MAC_F(it->address), param->read_rmt_name.rmt_name, it->scans_remaining);
            currentScan.erase(it);
          } else {
            it->next_scan_time = millis() + scan_delay_;
            if (it->scans_remaining == 0) {
              ESP_LOGW(TAG, "Device '%02X:%02X:%02X:%02X:%02X:%02X' not found on final scan. Removing from scan list.",
                       EXPAND_MAC_F(it->address));
              currentScan.erase(it);
            } else {
              ESP_LOGW(TAG, "Device '%02X:%02X:%02X:%02X:%02X:%02X' not found. %d scans remaining",
                       EXPAND_MAC_F(it->address), it->scans_remaining);
              // Put device at end of the scan queue
              if (currentScan.size() > 1) {
                moveItemToBack(currentScan, it - currentScan.begin());
              }
            }
          }
          scanPending_ = false;
          break;
        }
        it++;
      }

      if (currentScan.empty()) {
        ESP_LOGD(TAG, "Scan complete. No more devices left to scan.");
      }
    }
  }

 private:
  ESP32BtClassic *bt_client_;
  bool scanPending_{false};
  uint32_t scan_delay_{};
  uint32_t last_scan_ms{};
  std::vector<bt_scan_item> currentScan{};
};

template<typename... Ts> class BtClassicScanAction : public Action<Ts...>, public BtClassicScannerNode {
 public:
  BtClassicScanAction(ESP32BtClassic *bt_client) : BtClassicScannerNode(bt_client) {}

  void play(Ts... x) override {
    ESP_LOGI(TAG, "BtClassicScanAction::play()");
    uint16_t scanCount = this->num_scans_simple_;
    if (num_scans_template_ != nullptr) {
      scanCount = this->num_scans_template_(x...);
    }

    if (addr_template_ == nullptr) {
      return scan(addr_simple_, scanCount);
    } else {
      BtMacAddrVector template_results = addr_template_(x...);
      return scan(template_results, scanCount);
    }
  }

  void set_addr_template(std::function<std::vector<std::string>(Ts...)> func) {
    this->addr_template_ = std::move(func);
  }
  void set_addr_simple(const BtMacAddrVector &addr) { this->addr_simple_ = addr; }

  void set_num_scans_simple(uint16_t num_scans) { this->num_scans_simple_ = num_scans; }
  void set_num_scans_template(std::function<uint16_t(Ts...)> func) { this->num_scans_template_ = std::move(func); }

 private:
  uint16_t num_scans_simple_{1};
  BtMacAddrVector addr_simple_;
  std::function<std::vector<std::string>(Ts...)> addr_template_{};
  std::function<uint16_t(Ts...)> num_scans_template_{};
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
