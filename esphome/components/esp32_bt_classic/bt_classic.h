#pragma once

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

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

class ESP32BtClassic;

class GAPEventHandler {
 public:
  virtual void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) = 0;
};

struct BtGapEvent {
  explicit BtGapEvent(esp_bt_gap_cb_event_t Event, esp_bt_gap_cb_param_t *Param) : event(Event), param(*Param) {}
  esp_bt_gap_cb_event_t event;
  esp_bt_gap_cb_param_t param;
};

class bt_mac_addr {
 public:
  bt_mac_addr(const esp_bd_addr_t &address) { memcpy(addr, address, sizeof(esp_bd_addr_t)); }
  bt_mac_addr(uint64_t address) {
    uint8_t *s = (uint8_t *) &address;
    uint8_t *d = addr;

    for (int i = 5; i >= 0; i--) {
      d[i] = s[5 - i];
    }
  }
  bt_mac_addr(const char *address) {
    esp_bd_addr_t mac;
    if (strlen(address) < 12 || strlen(address) > 18) {
      return;
    }

    uint8_t *p = mac;
    // Scan for MAC with semicolon separators
    int args_found = sscanf(address, "%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8 ":%" SCNx8, &p[0], &p[1],
                            &p[2], &p[3], &p[4], &p[5]);
    if (args_found == 6) {
      memcpy(addr, mac, sizeof(esp_bd_addr_t));
      return;
    }

    // Scan for mac without semicolons
    args_found = sscanf(address, "%" SCNx8 "%" SCNx8 "%" SCNx8 "%" SCNx8 "%" SCNx8 "%" SCNx8, &p[0], &p[1], &p[2],
                        &p[3], &p[4], &p[5]);

    if (args_found == 6) {
      memcpy(addr, mac, sizeof(esp_bd_addr_t));
      return;
    }

    // Invalid MAC
    return;
  }

  bool operator==(const bt_mac_addr &rhs) { return 0 == memcmp(addr, rhs.addr, sizeof(addr)); }
  bool operator==(const esp_bd_addr_t &rhs) { return 0 == memcmp(addr, rhs, sizeof(addr)); }

  // operator esp_bd_addr_t() {
  //   return addr;
  // }

  // protected:
  esp_bd_addr_t addr;
};

class BtClassicNode {
 public:
  virtual void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) = 0;
  virtual void loop() {}

  // This should be transitioned to Established once the node no longer needs
  // the services/descriptors/characteristics of the parent client. This will
  // allow some memory to be freed.
  // espbt::ClientState node_state;

  ESP32BtClassic *parent() { return this->parent_; }
  void set_parent(ESP32BtClassic *parent) { this->parent_ = parent; }

 protected:
  ESP32BtClassic *parent_;
};

class ESP32BtClassic : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void register_gap_event_handler(GAPEventHandler *handler) { this->gap_event_handlers_.push_back(handler); }

  void register_node(BtClassicNode *node);

  void startScan(esp_bd_addr_t addr);

 protected:
  static void gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

  void real_gap_event_handler_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  void handle_gap_event_internal(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

  bool bt_setup_();
  void gap_init();
  bool gap_startup();

  typedef enum {
    APP_GAP_STATE_IDLE = 0,
    APP_GAP_STATE_DEVICE_DISCOVERING,
    APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE,
    APP_GAP_STATE_SERVICE_DISCOVERING,
    APP_GAP_STATE_SERVICE_DISCOVER_COMPLETE,
  } app_gap_state_t;

  typedef struct {
    bool dev_found;
    uint8_t bdname_len;
    uint8_t eir_len;
    uint8_t rssi;
    uint32_t cod;
    uint8_t eir[ESP_BT_GAP_EIR_DATA_LEN];
    uint8_t bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    esp_bd_addr_t bda;
    app_gap_state_t state;
  } app_gap_cb_t;
  app_gap_cb_t m_dev_info;

  std::vector<GAPEventHandler *> gap_event_handlers_;

  esp32_ble::Queue<BtGapEvent> bt_events_;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern ESP32BtClassic *global_bt_classic;

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
