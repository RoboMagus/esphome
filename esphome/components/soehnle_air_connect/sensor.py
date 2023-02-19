import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, ble_client, select, sensor, switch, text_sensor

from esphome.const import (
    STATE_CLASS_MEASUREMENT,
    CONF_BEEPER,
    CONF_FAN_MODE,
    CONF_FILTER,
    CONF_ID,
    CONF_POWER,
    CONF_PM_2_5,
    CONF_RAW,
    CONF_TEMPERATURE,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PM25,
    DEVICE_CLASS_TEMPERATURE,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    ICON_GRAIN,
    ICON_POWER,
    ICON_THERMOMETER,
    UNIT_CELSIUS,
    UNIT_MICROGRAMS_PER_CUBIC_METER,
    UNIT_PERCENT,
)

CONF_AUTO = "auto"
CONF_CONNECTED = "connected"
CONF_ENABLED = "enabled"
CONF_TIMER = "timer"
CONF_UVC = "uvc"
ICON_AUTO = "mdi:fan-auto"
ICON_CONNECT = "mdi:bluetooth-connect"
ICON_FILTER = "mdi:filter"

FAN_MODE_OPTIONS = ["low", "medium", "high", "turbo"]
TIMER_OPTIONS = ["off", "2hours", "4hours", "8hours"]

AUTO_LOAD = ["binary_sensor", "sensor", "switch", "select", "text_sensor"]
DEPENDENCIES = ["ble_client"]

soehnle_air_connect_ns = cg.esphome_ns.namespace("soehnle_air_connect")
Soehnle_AC500 = soehnle_air_connect_ns.class_(
    "Soehnle_AC500", cg.Component, ble_client.BLEClientNode
)

DeviceSelect = soehnle_air_connect_ns.class_(
    "DeviceSelect", select.Select, cg.Component
)
DeviceSwitch = soehnle_air_connect_ns.class_(
    "DeviceSwitch", switch.Switch, cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Soehnle_AC500),
            cv.Optional(CONF_FILTER, default={}): sensor.sensor_schema(
                unit_of_measurement=UNIT_PERCENT,
                icon=ICON_FILTER,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_TEMPERATURE,
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_PM_2_5, default={}): sensor.sensor_schema(
                unit_of_measurement=UNIT_MICROGRAMS_PER_CUBIC_METER,
                icon=ICON_GRAIN,
                accuracy_decimals=2,
                device_class=DEVICE_CLASS_PM25,
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_TEMPERATURE, default={}): sensor.sensor_schema(
                unit_of_measurement=UNIT_CELSIUS,
                icon=ICON_THERMOMETER,
                accuracy_decimals=2,
                state_class=STATE_CLASS_MEASUREMENT,
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_CONNECTED, default={}): binary_sensor.binary_sensor_schema(
                icon=ICON_CONNECT,
                device_class=DEVICE_CLASS_CONNECTIVITY,
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_POWER, default={}): switch.switch_schema(
                DeviceSwitch, icon=ICON_POWER
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_AUTO, default={}): switch.switch_schema(
                DeviceSwitch, icon=ICON_AUTO
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_BEEPER, default={}): switch.switch_schema(
                DeviceSwitch, icon="mdi:ear-hearing", entity_category=ENTITY_CATEGORY_CONFIG
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_UVC, default={}): switch.switch_schema(
                DeviceSwitch, icon="mdi:lightbulb-on"
            ).extend({cv.Optional(CONF_ENABLED, default=True): cv.boolean,}),
            cv.Optional(CONF_FAN_MODE, default={}): select.SELECT_SCHEMA.extend(
                {
                    cv.GenerateID(): cv.declare_id(DeviceSelect),
                    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
                }
            ),
            cv.Optional(CONF_TIMER, default={}): select.SELECT_SCHEMA.extend(
                {
                    cv.GenerateID(): cv.declare_id(DeviceSelect),
                    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
                }
            ),
            cv.Optional(CONF_RAW, default={}): text_sensor.text_sensor_schema(
                icon="mdi:hexadecimal", entity_category=ENTITY_CATEGORY_DIAGNOSTIC
            ).extend({cv.Optional(CONF_ENABLED, default=False): cv.boolean,}),
        }
    ).extend(ble_client.BLE_CLIENT_SCHEMA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await ble_client.register_ble_node(var, config)

    if CONF_CONNECTED in config and config[CONF_CONNECTED][CONF_ENABLED]:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(var.set_connected_sensor(sens))

    if CONF_PM_2_5 in config and config[CONF_PM_2_5][CONF_ENABLED]:
        sens = await sensor.new_sensor(config[CONF_PM_2_5])
        cg.add(var.set_particulate_sensor(sens))
    if CONF_TEMPERATURE in config and config[CONF_TEMPERATURE][CONF_ENABLED]:
        sens = await sensor.new_sensor(config[CONF_TEMPERATURE])
        cg.add(var.set_temperature_sensor(sens))
    if CONF_FILTER in config and config[CONF_FILTER][CONF_ENABLED]:
        sens = await sensor.new_sensor(config[CONF_FILTER])
        cg.add(var.set_filter_sensor(sens))

    if CONF_POWER in config and config[CONF_POWER][CONF_ENABLED]:
        swtch = await switch.new_switch(config[CONF_POWER])
        cg.add(var.set_power_switch(swtch))
    if CONF_AUTO in config and config[CONF_AUTO][CONF_ENABLED]:
        swtch = await switch.new_switch(config[CONF_AUTO])
        cg.add(var.set_auto_switch(swtch))
    if CONF_BEEPER in config and config[CONF_BEEPER][CONF_ENABLED]:
        swtch = await switch.new_switch(config[CONF_BEEPER])
        cg.add(var.set_beeper_switch(swtch))
    if CONF_UVC in config and config[CONF_UVC][CONF_ENABLED]:
        swtch = await switch.new_switch(config[CONF_UVC])
        cg.add(var.set_uvc_switch(swtch))

    if CONF_FAN_MODE in config and config[CONF_FAN_MODE][CONF_ENABLED]:
        slct = await select.new_select(config[CONF_FAN_MODE], options=FAN_MODE_OPTIONS)
        cg.add(var.set_fan_select(slct))
    if CONF_TIMER in config and config[CONF_TIMER][CONF_ENABLED]:
        slct = await select.new_select(config[CONF_TIMER], options=TIMER_OPTIONS)
        cg.add(var.set_timer_select(slct))

    if CONF_RAW in config and config[CONF_RAW][CONF_ENABLED]:
        sens = await text_sensor.new_text_sensor(config[CONF_RAW])
        cg.add(var.set_raw_sensor(sens))
