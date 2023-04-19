#ifdef USE_ESP32

#include "bt_classic.h"
#include "esphome/core/application.h"
#include "esphome/core/log.h"

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <freertos/FreeRTOS.h>
#include <freertos/FreeRTOSConfig.h>
#include <freertos/task.h>
#include <nvs_flash.h>

#ifdef USE_ARDUINO
#include <esp32-hal-bt.h>
#endif

namespace esphome {
namespace esp32_bt_classic {

static const char *const TAG = "esp32_bt_classic";

float ESP32BtClassic::get_setup_priority() const { return setup_priority::BLUETOOTH; }

void ESP32BtClassic::setup() {
  global_bt_classic = this;
  ESP_LOGCONFIG(TAG, "Setting up BT Classic...");

  if (!bt_setup_()) {
    ESP_LOGE(TAG, "BLE could not be set up");
    this->mark_failed();
    return;
  }

  ESP_LOGD(TAG, "BT Classic setup complete");
}

bool ESP32BtClassic::bt_setup_() {
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_flash_init failed: %d", err);
    return false;
  }

#ifdef USE_ARDUINO
  if (!btStart()) {
    ESP_LOGE(TAG, "btStart failed: %d", esp_bt_controller_get_status());
    return false;
  }
#else
  if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
    // start bt controller
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
      esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
      err = esp_bt_controller_init(&cfg);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_init failed: %s", esp_err_to_name(err));
        return false;
      }
      while (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE)
        ;
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
      err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_bt_controller_enable failed: %s", esp_err_to_name(err));
        return false;
      }
    }
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_ENABLED) {
      ESP_LOGE(TAG, "esp bt controller enable failed");
      return false;
    }
  }
#endif

  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

  err = esp_bluedroid_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", err);
    return false;
  }
  err = esp_bluedroid_enable();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", err);
    return false;
  }

  if (!this->gap_event_handlers_.empty()) {
    err = esp_bt_gap_register_callback(ESP32BtClassic::gap_event_handler);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_bt_gap_register_callback failed: %d", err);
      return false;
    }
  }

  std::string name = App.get_name();
  if (name.length() > 20) {
    if (App.is_name_add_mac_suffix_enabled()) {
      name.erase(name.begin() + 13, name.end() - 7);  // Remove characters between 13 and the mac address
    } else {
      name = name.substr(0, 20);
    }
  }

  // BT takes some time to be fully set up, 200ms should be more than enough
  delay(200);  // NOLINT

  return true;
}

void ESP32BtClassic::loop() {
  BtGapEvent *bt_event = this->bt_events_.pop();
  while (bt_event != nullptr) {
    this->real_gap_event_handler_(bt_event->event, &(bt_event->param));
    delete bt_event;  // NOLINT(cppcoreguidelines-owning-memory)
    bt_event = this->bt_events_.pop();
  }
}

void ESP32BtClassic::register_node(BtClassicNode *node) {
  node->set_parent(this);
  //  this->nodes_.push_back(node);
}

void ESP32BtClassic::startScan(esp_bd_addr_t addr) {
  ESP_LOGD(TAG, "Start scanning for %02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2], addr[3], addr[4],
           addr[5]);
}

void ESP32BtClassic::gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  BtGapEvent *new_event = new BtGapEvent(event, param);  // NOLINT(cppcoreguidelines-owning-memory)
  global_bt_classic->bt_events_.push(new_event);
}  // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void ESP32BtClassic::real_gap_event_handler_(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  ESP_LOGV(TAG, "(BT) gap_event_handler - %d", event);
  for (auto *gap_handler : this->gap_event_handlers_) {
    gap_handler->gap_event_handler(event, param);
  }
}

void ESP32BtClassic::dump_config() {
  const uint8_t *mac_address = esp_bt_dev_get_address();
  if (mac_address) {
    ESP_LOGCONFIG(TAG, "ESP32 BT Classic:");
    ESP_LOGCONFIG(TAG, "  MAC address: %02X:%02X:%02X:%02X:%02X:%02X", mac_address[0], mac_address[1], mac_address[2],
                  mac_address[3], mac_address[4], mac_address[5]);
  } else {
    ESP_LOGCONFIG(TAG, "ESP32 BT: bluetooth stack is not enabled");
  }
}

ESP32BtClassic *global_bt_classic = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
