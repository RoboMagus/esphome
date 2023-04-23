#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include "esphome/components/binary_sensor/binary_sensor.h"

#ifdef USE_ESP32

namespace esphome {
namespace esp32_bt_classic {

class BTClassicPresenceDevice : public PollingComponent,
                                public binary_sensor::BinarySensorInitiallyOff,
                                public BtClassicScanResultListner {
 public:
  BTClassicPresenceDevice(ESP32BtClassic *bt_client, uint64_t mac_address, uint8_t num_scans)
      : num_scans(num_scans), u64_addr(mac_address) {
    bt_client->register_scan_result_listener(this);
  }

  void update() override {
    scans_remaining = num_scans;
    parent()->addScan(bt_scan_item(u64_addr, num_scans));
  }

  void on_scan_result(const rmt_name_result &result) override {
    const uint64_t result_addr = bd_addr_to_uint64(result.bda);
    if (result_addr == u64_addr) {
      if (ESP_BT_STATUS_SUCCESS == result.stat) {
        this->publish_state(true);
      } else {
        if ((--scans_remaining) == 0) {
          this->publish_state(false);
        }
      }
    }
  }

 protected:
  uint8_t scans_remaining{0};
  const uint8_t num_scans;
  const uint64_t u64_addr;
};

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
