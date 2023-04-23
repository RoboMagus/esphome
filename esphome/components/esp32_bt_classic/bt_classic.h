#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "esphome/components/esp32_ble/queue.h"

#ifdef USE_ESP32

// IDF headers
#include <esp_bt_defs.h>
#include <esp_gap_bt_api.h>

#ifndef SCNx8
#define SCNx8 "hhx"
#endif

// Helper for printing Bt MAC addresses for format "%02X:%02X:%02X:%02X:%02X:%02X"
#define EXPAND_MAC_F(addr) addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]

namespace esphome {
namespace esp32_bt_classic {

// ToDo:
// [x] Hook up nodes callback functions.
// [x] Refactor to use u64 based mac address (common among EspHome codebase)
//     - u64 internal and convert to esp_bd_addr_t at the HAL
// [ ] Refactor Automation Trigger interface into string status, string mac, and string name

static const char *const TAG = "esp32_bt_classic";

class ESP32BtClassic;

typedef esp_bt_gap_cb_param_t::read_rmt_name_param rmt_name_result;

// bd_addr_t <--> uint64_t conversion functions:
void uint64_to_bd_addr(uint64_t address, esp_bd_addr_t &bd_addr);
uint64_t bd_addr_to_uint64(const esp_bd_addr_t address);

std::string bd_addr_to_str(const esp_bd_addr_t &addr);
bool str_to_bd_addr(const char *addr_str, esp_bd_addr_t &addr);

struct BtGapEvent {
  explicit BtGapEvent(esp_bt_gap_cb_event_t Event, esp_bt_gap_cb_param_t *Param) : event(Event), param(*Param) {}
  esp_bt_gap_cb_event_t event;
  esp_bt_gap_cb_param_t param;
};

struct bt_scan_item {
  bt_scan_item(uint64_t u64_addr, uint8_t num_scans) : address(u64_addr), scans_remaining(num_scans) {}
  uint64_t address;
  uint8_t scans_remaining;
  uint32_t next_scan_time;
};

class BtClassicItf {
 public:
  virtual void addScan(const bt_scan_item &scan) = 0;
  virtual void addScan(const std::vector<bt_scan_item> &scan_list) = 0;
};

class BtClassicChildBase {
 public:
  BtClassicItf *parent() { return this->parent_; }
  void set_parent(BtClassicItf *parent) { parent_ = parent; }

 protected:
  BtClassicItf *parent_{nullptr};
};

class BtClassicScanStartListner : public BtClassicChildBase {
 public:
  virtual void on_scan_start() = 0;
};

class BtClassicScanResultListner : public BtClassicChildBase {
 public:
  virtual void on_scan_result(const rmt_name_result &result) = 0;
};

// -----------------------------------------------
// Main BT Classic class:
//
class ESP32BtClassic : public Component, public BtClassicItf {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void register_scan_start_listener(BtClassicScanStartListner *listner) {
    listner->set_parent(this);
    scan_start_listners_.push_back(listner);
  }
  void register_scan_result_listener(BtClassicScanResultListner *listner) {
    listner->set_parent(this);
    scan_result_listners_.push_back(listner);
  }

  // Interface functions:
  void addScan(const bt_scan_item &scan) override;
  void addScan(const std::vector<bt_scan_item> &scan_list) override;

 protected:
  static void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  void real_gap_event_handler_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

  void startScan(const uint64_t u64_addr);

  void handle_scan_result(const rmt_name_result &result);

  bool bt_setup_();
  bool gap_startup();

  bool scanPending_{false};
  uint32_t last_scan_ms{};
  std::vector<bt_scan_item> active_scan_list_{};

  // Listners a.o.
  std::vector<BtClassicScanStartListner *> scan_start_listners_;
  std::vector<BtClassicScanResultListner *> scan_result_listners_;

  // Ble-Queue which thread safety precautions:
  esp32_ble::Queue<BtGapEvent> bt_events_;

  const uint32_t scan_delay_{100};  // (ms) minimal time between consecutive scans
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern ESP32BtClassic *global_bt_classic;

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
