#ifdef USE_ESP32

#include "bt_classic.h"
#include "bt_status.h"
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

std::string addr2str(const esp_bd_addr_t &addr) {
  char mac[24];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", EXPAND_MAC_F(addr));
  return mac;
}

float ESP32BtClassic::get_setup_priority() const {
  // Setup just after BLE, (but before AFTER_BLUETOOTH) to ensure both can co-exist!
  return setup_priority::BLUETOOTH - 5.0f;
}

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

  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
    if ((err = esp_bluedroid_init()) != ESP_OK) {
      ESP_LOGE(TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(err));
      return false;
    }
  }

  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
    if ((err = esp_bluedroid_enable()) != ESP_OK) {
      ESP_LOGE(TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(err));
      return false;
    }
  }

  bool success = gap_startup();

  // BT takes some time to be fully set up, 200ms should be more than enough
  delay(200);  // NOLINT

  return success;
}

void ESP32BtClassic::gap_init() {
  app_gap_cb_t *p_dev = &m_dev_info;
  memset(p_dev, 0, sizeof(app_gap_cb_t));
}

bool ESP32BtClassic::gap_startup() {
  const char *dev_name = "ESP32_scanner";
  esp_bt_dev_set_device_name(dev_name);

  // inititialize device information and status
  gap_init();

  // set discoverable and connectable mode, wait to be connected
  esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

  // register GAP callback function
  // if (!this->gap_event_handlers_.empty()) {
  {
    esp_err_t err = esp_bt_gap_register_callback(ESP32BtClassic::gap_event_handler);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_bt_gap_register_callback failed: %s", esp_err_to_name(err));
      return false;
    }
  }

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
  ESP_LOGD(TAG, "Start scanning for %02X:%02X:%02X:%02X:%02X:%02X", EXPAND_MAC_F(addr), addr[5]);

  esp_bt_gap_read_remote_name(addr);
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

  // Internal handling:
  handle_gap_event_internal(event, param);
}

void ESP32BtClassic::handle_gap_event_internal(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param) {
  switch (event) {
    case ESP_BT_GAP_READ_REMOTE_NAME_EVT: {
      // SetReadRemoteNameResult(param->read_rmt_name);
      ESP_LOGI(TAG, "Read remote name result:\n  Stat: %s (%d)\n  Name: %s\n  Addr: %02X:%02X:%02X:%02X:%02X:%02X",
               esp_bt_status_to_str(param->read_rmt_name.stat), param->read_rmt_name.stat,
               param->read_rmt_name.rmt_name, EXPAND_MAC_F(param->read_rmt_name.bda));

      for (auto *listener : this->result_listners_) {
        listener->on_scan_result(param->read_rmt_name);
      }
      break;
    }
    default: {
      ESP_LOGI(TAG, "event: %d", event);
      break;
    }
  }
}

void ESP32BtClassic::dump_config() {
  const uint8_t *mac_address = esp_bt_dev_get_address();
  if (mac_address) {
    ESP_LOGCONFIG(TAG, "ESP32 BT Classic:");
    ESP_LOGCONFIG(TAG, "  MAC address: %02X:%02X:%02X:%02X:%02X:%02X", EXPAND_MAC_F(mac_address));
  } else {
    ESP_LOGCONFIG(TAG, "ESP32 BT: bluetooth stack is not enabled");
  }
}

ESP32BtClassic *global_bt_classic = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif
