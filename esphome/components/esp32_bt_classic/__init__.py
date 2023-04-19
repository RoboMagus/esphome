import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_DELAY, CONF_ID, CONF_MAC_ADDRESS
from esphome.core import CORE
from esphome import automation
from esphome.components.esp32 import add_idf_sdkconfig_option, get_esp32_variant, const

_LOGGER = logging.getLogger(__name__)

AUTO_LOAD = ["esp32_ble"]
DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@RoboMagus"]
CONFLICTS_WITH = ["esp32_ble_beacon"]

CONF_BLE_ID = "ble_id"

NO_BLUTOOTH_VARIANTS = [const.VARIANT_ESP32S2]

MIN_IDF_VERSION = cv.Version(4, 4, 4)
MIN_ARDUINO_VERSION = cv.Version(2, 0, 6)

esp32_bt_classic_ns = cg.esphome_ns.namespace("esp32_bt_classic")
ESP32BtClassic = esp32_bt_classic_ns.class_("ESP32BtClassic", cg.Component)

GAPEventHandler = esp32_bt_classic_ns.class_("GAPEventHandler")

# Actions
BtClassicScanAction = esp32_bt_classic_ns.class_(
    "BtClassicScanAction", automation.Action
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ESP32BtClassic),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.require_framework_version(
        esp_idf=MIN_IDF_VERSION,
        esp32_arduino=MIN_ARDUINO_VERSION,
        extra_message="Because of ESP-IDF compatibility...",
    ),
)


def validate_variant(_):
    variant = get_esp32_variant()
    if variant in NO_BLUTOOTH_VARIANTS:
        raise cv.Invalid(f"{variant} does not support Bluetooth")


FINAL_VALIDATE_SCHEMA = validate_variant


BT_CLASSIC_SCAN_ACTION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.use_id(ESP32BtClassic),
        cv.Required(CONF_MAC_ADDRESS): cv.templatable(cv.ensure_list(cv.mac_address)),
        cv.Optional(CONF_DELAY, default="0s"): cv.positive_time_period_milliseconds,
    }
)


@automation.register_action(
    "bt_classic.bt_classic_scan", BtClassicScanAction, BT_CLASSIC_SCAN_ACTION_SCHEMA
)
async def bt_classic_scan_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)

    addr = config[CONF_MAC_ADDRESS]
    _LOGGER.warning("Scan addresses: %s", addr)
    if cg.is_template(addr):
        templ = await cg.templatable(
            addr, args, cg.std_vector.template(cg.global_ns.namespace("esp_bd_addr_t"))
        )
        cg.add(var.set_addr_template(templ))
    else:
        addr_list = []
        for it in addr:
            addr_list.append(it.as_hex)
        _LOGGER.warning("Scan addresses list: %s", addr_list)
        cg.add(var.set_addr_simple(addr_list))

    if CONF_DELAY in config:
        cg.add(var.set_scan_delay(config[CONF_DELAY]))

    return var


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CORE.using_esp_idf:
        add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
