#pragma once

#ifdef USE_ESP32
#include <esp_bt_defs.h>

typedef struct {
  esp_bt_status_t code;
  const char *msg;
} esp_bt_status_msg_t;

const char *esp_bt_status_to_str(esp_bt_status_t code);

#endif  // USE_ESP32
