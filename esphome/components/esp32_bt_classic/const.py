import esphome.codegen as cg

CONF_ESP32_BTCLASSIC_ID = "esp32_btc_id"

CONF_NUM_SCANS = "num_scans"

CONF_ON_SCAN_START = "on_scan_start"
CONF_ON_SCAN_RESULT = "on_scan_result"

esp32_bt_classic_ns = cg.esphome_ns.namespace("esp32_bt_classic")
ESP32BtClassic = esp32_bt_classic_ns.class_("ESP32BtClassic", cg.Component)
