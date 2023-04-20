
#ifdef USE_ESP32
#include "bt_status.h"

#define STRINGIZE(x) #x
#define TABLE_ENTRY(x) x, #x

namespace esphome {
namespace esp32_bt_classic {

static const esp_bt_status_msg_t esp_bt_status_msg_table[] = {
    TABLE_ENTRY(ESP_BT_STATUS_SUCCESS),
    TABLE_ENTRY(ESP_BT_STATUS_FAIL),
    TABLE_ENTRY(ESP_BT_STATUS_NOT_READY),
    TABLE_ENTRY(ESP_BT_STATUS_NOMEM),
    TABLE_ENTRY(ESP_BT_STATUS_BUSY),
    TABLE_ENTRY(ESP_BT_STATUS_DONE),
    TABLE_ENTRY(ESP_BT_STATUS_UNSUPPORTED),
    TABLE_ENTRY(ESP_BT_STATUS_PARM_INVALID),
    TABLE_ENTRY(ESP_BT_STATUS_UNHANDLED),
    TABLE_ENTRY(ESP_BT_STATUS_AUTH_FAILURE),
    TABLE_ENTRY(ESP_BT_STATUS_RMT_DEV_DOWN),
    TABLE_ENTRY(ESP_BT_STATUS_AUTH_REJECTED),
    TABLE_ENTRY(ESP_BT_STATUS_INVALID_STATIC_RAND_ADDR),
    TABLE_ENTRY(ESP_BT_STATUS_PENDING),
    TABLE_ENTRY(ESP_BT_STATUS_UNACCEPT_CONN_INTERVAL),
    TABLE_ENTRY(ESP_BT_STATUS_PARAM_OUT_OF_RANGE),
    TABLE_ENTRY(ESP_BT_STATUS_TIMEOUT),
    TABLE_ENTRY(ESP_BT_STATUS_PEER_LE_DATA_LEN_UNSUPPORTED),
    TABLE_ENTRY(ESP_BT_STATUS_CONTROL_LE_DATA_LEN_UNSUPPORTED),
    TABLE_ENTRY(ESP_BT_STATUS_ERR_ILLEGAL_PARAMETER_FMT),
    TABLE_ENTRY(ESP_BT_STATUS_MEMORY_FULL),
    TABLE_ENTRY(ESP_BT_STATUS_EIR_TOO_LARGE)};

const char *esp_bt_status_to_str(esp_bt_status_t code) {
  for (int i = 0; i < sizeof(esp_bt_status_msg_table) / sizeof(esp_bt_status_msg_table[0]); ++i) {
    if (esp_bt_status_msg_table[i].code == code) {
      return esp_bt_status_msg_table[i].msg;
    }
  }

  return "Unknown Status";
}

}  // namespace esp32_bt_classic
}  // namespace esphome

#endif  // USE_ESP32
