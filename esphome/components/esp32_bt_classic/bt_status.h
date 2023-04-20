#pragma once

#ifdef USE_ESP32
#include <esp_bt_defs.h>

namespace esphome {
namespace esp32_bt_classic {

typedef struct {
  esp_bt_status_t code;
  const char *msg;
} esp_bt_status_msg_t;

const char *esp_bt_status_to_str(esp_bt_status_t code);

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif  // USE_ESP32
