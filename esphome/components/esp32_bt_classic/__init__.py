import logging

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_DELAY,
    CONF_ID,
    CONF_MAC_ADDRESS,
    CONF_TRIGGER_ID,
)
from esphome.core import CORE
from esphome import automation
from esphome.components.esp32 import add_idf_sdkconfig_option, get_esp32_variant, const

_LOGGER = logging.getLogger(__name__)

AUTO_LOAD = ["esp32_ble"]
DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@RoboMagus"]
CONFLICTS_WITH = ["esp32_ble_beacon"]

CONF_NUM_SCANS = "num_scans"
CONF_ON_SCAN_RESULT = "on_scan_result"

NO_BLUTOOTH_VARIANTS = [const.VARIANT_ESP32S2]

MIN_IDF_VERSION = cv.Version(4, 4, 4)
MIN_ARDUINO_VERSION = cv.Version(2, 0, 6)

esp32_bt_classic_ns = cg.esphome_ns.namespace("esp32_bt_classic")
ESP32BtClassic = esp32_bt_classic_ns.class_("ESP32BtClassic", cg.Component)

RMT_NAME_RESULT = esp32_bt_classic_ns.class_("rmt_name_result")
RMT_NAME_RESULTConstRef = RMT_NAME_RESULT.operator("ref").operator("const")

GAPEventHandler = esp32_bt_classic_ns.class_("GAPEventHandler")

# Actions
BtClassicScanAction = esp32_bt_classic_ns.class_(
    "BtClassicScanAction", automation.Action
)
# Triggers
BtClassicScanResultTrigger = esp32_bt_classic_ns.class_(
    "BtClassicScanResultTrigger", automation.Trigger.template(RMT_NAME_RESULTConstRef)
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ESP32BtClassic),
            cv.Optional(CONF_ON_SCAN_RESULT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        BtClassicScanResultTrigger
                    ),
                    cv.Optional(CONF_MAC_ADDRESS): cv.mac_address,
                }
            ),
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
        cv.Optional(CONF_NUM_SCANS, default=1): cv.templatable(cv.positive_int),
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
    if cg.is_template(addr):
        templ = await cg.templatable(addr, args, cg.std_vector.template(cg.std_string))
        cg.add(var.set_addr_template(templ))
    else:
        addr_list = []
        for it in addr:
            addr_list.append(it.as_hex)
        cg.add(var.set_addr_simple(addr_list))

    if CONF_NUM_SCANS in config:
        if cg.is_template(config[CONF_NUM_SCANS]):
            templ = await cg.templatable(config[CONF_NUM_SCANS], args, cg.uint16)
            cg.add(var.set_num_scans_template(templ))
        else:
            cg.add(var.set_num_scans_simple(config[CONF_NUM_SCANS]))

    if CONF_DELAY in config:
        cg.add(var.set_scan_delay(config[CONF_DELAY]))

    return var


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for conf in config.get(CONF_ON_SCAN_RESULT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        if CONF_MAC_ADDRESS in conf:
            cg.add(trigger.set_address(conf[CONF_MAC_ADDRESS].as_hex))
        await automation.build_automation(
            trigger, [(RMT_NAME_RESULTConstRef, "x")], conf
        )

    if CORE.using_esp_idf:
        add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
