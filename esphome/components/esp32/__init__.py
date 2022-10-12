from dataclasses import dataclass
from typing import Union
from pathlib import Path
import logging
import os

from esphome.helpers import copy_file_if_changed, write_file_if_changed
from esphome.const import (
    CONF_BOARD,
    CONF_FRAMEWORK,
    CONF_SOURCE,
    CONF_TYPE,
    CONF_VARIANT,
    CONF_VERSION,
    CONF_ADVANCED,
    CONF_IGNORE_EFUSE_MAC_CRC,
    KEY_CORE,
    KEY_FRAMEWORK_VERSION,
    KEY_TARGET_FRAMEWORK,
    KEY_TARGET_PLATFORM,
    __version__,
)
from esphome.core import CORE, HexInt
import esphome.config_validation as cv
import esphome.codegen as cg

from .const import (  # noqa
    KEY_BOARD,
    KEY_ESP32,
    KEY_SDKCONFIG_OPTIONS,
    KEY_VARIANT,
    VARIANT_ESP32C3,
    VARIANT_FRIENDLY,
    VARIANTS,
)
from .boards import BOARD_TO_VARIANT

# force import gpio to register pin schema
from .gpio import esp32_pin_to_code  # noqa


_LOGGER = logging.getLogger(__name__)
CODEOWNERS = ["@esphome/core"]
AUTO_LOAD = ["preferences"]

REAL_TARGET_FRAMEWORK = "real_target_framework"


def set_core_data(config):
    CORE.data[KEY_ESP32] = {}
    CORE.data[KEY_CORE][KEY_TARGET_PLATFORM] = "esp32"
    conf = config[CONF_FRAMEWORK]
    if conf[CONF_TYPE] == FRAMEWORK_ESP_IDF:
        CORE.data[KEY_CORE][REAL_TARGET_FRAMEWORK] = "esp-idf"
        CORE.data[KEY_CORE][KEY_TARGET_FRAMEWORK] = "esp-idf"
        CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS] = {}
    elif conf[CONF_TYPE] == FRAMEWORK_ARDUINO:
        CORE.data[KEY_CORE][REAL_TARGET_FRAMEWORK] = "arduino"
        CORE.data[KEY_CORE][KEY_TARGET_FRAMEWORK] = "arduino"
    elif conf[CONF_TYPE] == FRAMEWORK_ARDUINO_IDF:
        CORE.data[KEY_CORE][REAL_TARGET_FRAMEWORK] = "arduino-idf"
        CORE.data[KEY_CORE][KEY_TARGET_FRAMEWORK] = "arduino"
        CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS] = {}
    CORE.data[KEY_CORE][KEY_FRAMEWORK_VERSION] = cv.Version.parse(
        config[CONF_FRAMEWORK][CONF_VERSION]
    )
    CORE.data[KEY_ESP32][KEY_BOARD] = config[CONF_BOARD]
    CORE.data[KEY_ESP32][KEY_VARIANT] = config[CONF_VARIANT]
    return config


def get_esp32_variant(core_obj=None):
    return (core_obj or CORE).data[KEY_ESP32][KEY_VARIANT]


def only_on_variant(*, supported=None, unsupported=None):
    """Config validator for features only available on some ESP32 variants."""
    if supported is not None and not isinstance(supported, list):
        supported = [supported]
    if unsupported is not None and not isinstance(unsupported, list):
        unsupported = [unsupported]

    def validator_(obj):
        variant = get_esp32_variant()
        if supported is not None and variant not in supported:
            raise cv.Invalid(
                f"This feature is only available on {', '.join(supported)}"
            )
        if unsupported is not None and variant in unsupported:
            raise cv.Invalid(
                f"This feature is not available on {', '.join(unsupported)}"
            )
        return obj

    return validator_


@dataclass
class RawSdkconfigValue:
    """An sdkconfig value that won't be auto-formatted"""

    value: str


SdkconfigValueType = Union[bool, int, HexInt, str, RawSdkconfigValue]


def add_idf_sdkconfig_option(name: str, value: SdkconfigValueType):
    """Set an esp-idf sdkconfig value."""
    if CORE.data[KEY_CORE][REAL_TARGET_FRAMEWORK] == "arduino":
        raise ValueError("Not an esp-idf project")
    CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS][name] = value


def _format_framework_arduino_version(ver: cv.Version) -> str:
    # format the given arduino (https://github.com/espressif/arduino-esp32/releases) version to
    # a PIO platformio/framework-arduinoespressif32 value
    # List of package versions: https://api.registry.platformio.org/v3/packages/platformio/tool/framework-arduinoespressif32
    if ver <= cv.Version(1, 0, 3):
        return f"~2.{ver.major}{ver.minor:02d}{ver.patch:02d}.0"
    return f"~3.{ver.major}{ver.minor:02d}{ver.patch:02d}.0"


def _format_framework_espidf_version(ver: cv.Version) -> str:
    # format the given arduino (https://github.com/espressif/esp-idf/releases) version to
    # a PIO platformio/framework-espidf value
    # List of package versions: https://api.registry.platformio.org/v3/packages/platformio/tool/framework-espidf
    return f"~3.{ver.major}{ver.minor:02d}{ver.patch:02d}.0"


# NOTE: Keep this in mind when updating the recommended version:
#  * New framework historically have had some regressions, especially for WiFi.
#    The new version needs to be thoroughly validated before changing the
#    recommended version as otherwise a bunch of devices could be bricked
#  * For all constants below, update platformio.ini (in this repo)

# The default/recommended arduino framework version
#  - https://github.com/espressif/arduino-esp32/releases
#  - https://api.registry.platformio.org/v3/packages/platformio/tool/framework-arduinoespressif32
RECOMMENDED_ARDUINO_FRAMEWORK_VERSION = cv.Version(1, 0, 6)
# The platformio/espressif32 version to use for arduino frameworks
#  - https://github.com/platformio/platform-espressif32/releases
#  - https://api.registry.platformio.org/v3/packages/platformio/platform/espressif32
ARDUINO_PLATFORM_VERSION = cv.Version(3, 5, 0)

# The default/recommended esp-idf framework version
#  - https://github.com/espressif/esp-idf/releases
#  - https://api.registry.platformio.org/v3/packages/platformio/tool/framework-espidf
RECOMMENDED_ESP_IDF_FRAMEWORK_VERSION = cv.Version(4, 3, 2)
# The platformio/espressif32 version to use for esp-idf frameworks
#  - https://github.com/platformio/platform-espressif32/releases
#  - https://api.registry.platformio.org/v3/packages/platformio/platform/espressif32
ESP_IDF_PLATFORM_VERSION = cv.Version(3, 5, 0)


def _arduino_check_versions(value):
    value = value.copy()
    lookups = {
        "dev": (cv.Version(2, 0, 0), "https://github.com/espressif/arduino-esp32.git"),
        "latest": (cv.Version(1, 0, 6), None),
        "recommended": (RECOMMENDED_ARDUINO_FRAMEWORK_VERSION, None),
    }

    if value[CONF_VERSION] in lookups:
        if CONF_SOURCE in value:
            raise cv.Invalid(
                "Framework version needs to be explicitly specified when custom source is used."
            )

        version, source = lookups[value[CONF_VERSION]]
    else:
        version = cv.Version.parse(cv.version_number(value[CONF_VERSION]))
        source = value.get(CONF_SOURCE, None)

    value[CONF_VERSION] = str(version)
    value[CONF_SOURCE] = source or _format_framework_arduino_version(version)

    value[CONF_PLATFORM_VERSION] = value.get(
        CONF_PLATFORM_VERSION, _parse_platform_version(str(ARDUINO_PLATFORM_VERSION))
    )

    if version != RECOMMENDED_ARDUINO_FRAMEWORK_VERSION:
        _LOGGER.warning(
            "The selected Arduino framework version is not the recommended one. "
            "If there are connectivity or build issues please remove the manual version."
        )

    return value


def _esp_arduino_idf_check_versions(value):
    _LOGGER.warning(
        "Using Arduino-IDF framework is experimental and not reccommended!"
        "If there are connectivity or build issues please revert to either of the supported frameworks. (e.g. 'arduino' or 'esp-idf')"
    )
    _LOGGER.warning(
        "If your build fails using Arduino-IDF framework, please explicitly clean your project before building again. Some modifications are not picked up correctly and may cause build failures..."
    )
    return _esp_idf_check_versions(value)


def _esp_idf_check_versions(value):
    value = value.copy()
    lookups = {
        "dev": (cv.Version(5, 0, 0), "https://github.com/espressif/esp-idf.git"),
        "latest": (cv.Version(4, 3, 2), None),
        "recommended": (RECOMMENDED_ESP_IDF_FRAMEWORK_VERSION, None),
    }

    if value[CONF_VERSION] in lookups:
        if CONF_SOURCE in value:
            raise cv.Invalid(
                "Framework version needs to be explicitly specified when custom source is used."
            )

        version, source = lookups[value[CONF_VERSION]]
    else:
        version = cv.Version.parse(cv.version_number(value[CONF_VERSION]))
        source = value.get(CONF_SOURCE, None)

    if version < cv.Version(4, 0, 0):
        raise cv.Invalid("Only ESP-IDF 4.0+ is supported.")

    value[CONF_VERSION] = str(version)
    value[CONF_SOURCE] = source or _format_framework_espidf_version(version)

    value[CONF_PLATFORM_VERSION] = value.get(
        CONF_PLATFORM_VERSION, _parse_platform_version(str(ESP_IDF_PLATFORM_VERSION))
    )

    if version != RECOMMENDED_ESP_IDF_FRAMEWORK_VERSION:
        _LOGGER.warning(
            "The selected ESP-IDF framework version is not the recommended one. "
            "If there are connectivity or build issues please remove the manual version."
        )

    return value


def _parse_platform_version(value):
    try:
        # if platform version is a valid version constraint, prefix the default package
        cv.platformio_version_constraint(value)
        return f"platformio/espressif32 @ {value}"
    except cv.Invalid:
        return value


def _detect_variant(value):
    if CONF_VARIANT not in value:
        board = value[CONF_BOARD]
        if board not in BOARD_TO_VARIANT:
            raise cv.Invalid(
                "This board is unknown, please set the variant manually",
                path=[CONF_BOARD],
            )

        value = value.copy()
        value[CONF_VARIANT] = BOARD_TO_VARIANT[board]

    return value


CONF_PLATFORM_VERSION = "platform_version"

ARDUINO_FRAMEWORK_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_VERSION, default="recommended"): cv.string_strict,
            cv.Optional(CONF_SOURCE): cv.string_strict,
            cv.Optional(CONF_PLATFORM_VERSION): _parse_platform_version,
        }
    ),
    _arduino_check_versions,
)

CONF_SDKCONFIG_OPTIONS = "sdkconfig_options"
ESP_IDF_FRAMEWORK_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_VERSION): cv.string_strict,
            cv.Required(CONF_SOURCE): cv.string_strict,
            cv.Required(CONF_PLATFORM_VERSION): _parse_platform_version,
            cv.Optional(CONF_SDKCONFIG_OPTIONS, default={}): {
                cv.string_strict: cv.string_strict
            },
            cv.Optional(CONF_ADVANCED, default={}): cv.Schema(
                {
                    cv.Optional(CONF_IGNORE_EFUSE_MAC_CRC, default=False): cv.boolean,
                }
            ),
        }
    ),
    _esp_idf_check_versions,
)

ARDUINO_IDF_FRAMEWORK_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_VERSION, default="recommended"): cv.string_strict,
            cv.Optional(CONF_SOURCE): cv.string_strict,
            cv.Optional(CONF_PLATFORM_VERSION): _parse_platform_version,
            cv.Optional(CONF_SDKCONFIG_OPTIONS, default={}): {
                cv.string_strict: cv.string_strict
            },
            cv.Optional(CONF_ADVANCED, default={}): cv.Schema(
                {
                    cv.Optional(CONF_IGNORE_EFUSE_MAC_CRC, default=False): cv.boolean,
                }
            ),
        }
    ),
    _esp_arduino_idf_check_versions,
)


FRAMEWORK_ESP_IDF = "esp-idf"
FRAMEWORK_ARDUINO = "arduino"
FRAMEWORK_ARDUINO_IDF = "arduino-idf"
FRAMEWORK_SCHEMA = cv.typed_schema(
    {
        FRAMEWORK_ESP_IDF: ESP_IDF_FRAMEWORK_SCHEMA,
        FRAMEWORK_ARDUINO: ARDUINO_FRAMEWORK_SCHEMA,
        FRAMEWORK_ARDUINO_IDF: ARDUINO_IDF_FRAMEWORK_SCHEMA,
    },
    lower=True,
    space="-",
    default_type=FRAMEWORK_ARDUINO,
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Required(CONF_BOARD): cv.string_strict,
            cv.Optional(CONF_VARIANT): cv.one_of(*VARIANTS, upper=True),
            cv.Optional(CONF_FRAMEWORK, default={}): FRAMEWORK_SCHEMA,
        }
    ),
    _detect_variant,
    set_core_data,
)


async def to_code(config):
    cg.add_platformio_option("board", config[CONF_BOARD])
    cg.add_build_flag("-DUSE_ESP32")
    cg.add_define("ESPHOME_BOARD", config[CONF_BOARD])
    cg.add_build_flag(f"-DUSE_ESP32_VARIANT_{config[CONF_VARIANT]}")
    cg.add_define("ESPHOME_VARIANT", VARIANT_FRIENDLY[config[CONF_VARIANT]])

    cg.add_platformio_option("lib_ldf_mode", "off")

    framework_ver: cv.Version = CORE.data[KEY_CORE][KEY_FRAMEWORK_VERSION]

    conf = config[CONF_FRAMEWORK]
    cg.add_platformio_option("platform", conf[CONF_PLATFORM_VERSION])

    cg.add_platformio_option(
        "extra_scripts", ["pre:post_build.py", "post:post_build.py"]
    )

    if conf[CONF_TYPE] == FRAMEWORK_ESP_IDF:
        cg.add_platformio_option("framework", "espidf")
        cg.add_build_flag("-DUSE_ESP_IDF")
        cg.add_build_flag("-DUSE_ESP32_FRAMEWORK_ESP_IDF")
        cg.add_build_flag("-Wno-nonnull-compare")
        cg.add_platformio_option(
            "platform_packages",
            [f"platformio/framework-espidf @ {conf[CONF_SOURCE]}"],
        )
        add_idf_sdkconfig_option("CONFIG_PARTITION_TABLE_SINGLE_APP", False)
        add_idf_sdkconfig_option("CONFIG_PARTITION_TABLE_CUSTOM", True)
        add_idf_sdkconfig_option(
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "partitions.csv"
        )
        add_idf_sdkconfig_option("CONFIG_COMPILER_OPTIMIZATION_DEFAULT", False)
        add_idf_sdkconfig_option("CONFIG_COMPILER_OPTIMIZATION_SIZE", True)

        # Increase freertos tick speed from 100Hz to 1kHz so that delay() resolution is 1ms
        add_idf_sdkconfig_option("CONFIG_FREERTOS_HZ", 1000)

        # Setup watchdog
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT", True)
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_PANIC", True)
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0", False)
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1", False)

        cg.add_platformio_option("board_build.partitions", "partitions.csv")

        for name, value in conf[CONF_SDKCONFIG_OPTIONS].items():
            add_idf_sdkconfig_option(name, RawSdkconfigValue(value))

        if conf[CONF_ADVANCED][CONF_IGNORE_EFUSE_MAC_CRC]:
            cg.add_define("USE_ESP32_IGNORE_EFUSE_MAC_CRC")
            add_idf_sdkconfig_option(
                "CONFIG_ESP32_PHY_CALIBRATION_AND_DATA_STORAGE", False
            )

        cg.add_define(
            "USE_ESP_IDF_VERSION_CODE",
            cg.RawExpression(
                f"VERSION_CODE({framework_ver.major}, {framework_ver.minor}, {framework_ver.patch})"
            ),
        )

    elif conf[CONF_TYPE] == FRAMEWORK_ARDUINO:
        cg.add_platformio_option("framework", "arduino")
        cg.add_build_flag("-DUSE_ARDUINO")
        cg.add_build_flag("-DUSE_ESP32_FRAMEWORK_ARDUINO")
        cg.add_platformio_option(
            "platform_packages",
            [f"platformio/framework-arduinoespressif32 @ {conf[CONF_SOURCE]}"],
        )

        cg.add_platformio_option("board_build.partitions", "partitions.csv")

        cg.add_define(
            "USE_ARDUINO_VERSION_CODE",
            cg.RawExpression(
                f"VERSION_CODE({framework_ver.major}, {framework_ver.minor}, {framework_ver.patch})"
            ),
        )

    elif conf[CONF_TYPE] == FRAMEWORK_ARDUINO_IDF:
        # Currently hard-coded as it is known to work!
        # ToDo: Make version configurable...
        cg.add_platformio_option(
            "platform",
            "https://github.com/tasmota/platform-espressif32/releases/download/v2.0.4.1/platform-espressif32-2.0.4.1.zip",
        )
        cg.add_platformio_option("framework", "arduino, espidf")
        cg.add_platformio_option("board_build.partitions", "partitions.csv")
        cg.add_build_flag("-DUSE_ARDUINO")
        cg.add_build_flag("-DUSE_ARDUINO_IDF")
        cg.add_build_flag("-DUSE_ESP32_FRAMEWORK_ARDUINO")
        cg.add_build_flag("-Wno-nonnull-compare")
        cg.add_build_flag("-Wno-misleading-indentation")

        cg.add_platformio_option("lib_ldf_mode", "chain")

        add_idf_sdkconfig_option("CONFIG_PARTITION_TABLE_SINGLE_APP", False)
        add_idf_sdkconfig_option("CONFIG_PARTITION_TABLE_CUSTOM", True)
        add_idf_sdkconfig_option(
            "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "partitions.csv"
        )
        add_idf_sdkconfig_option("CONFIG_COMPILER_OPTIMIZATION_DEFAULT", False)
        add_idf_sdkconfig_option("CONFIG_COMPILER_OPTIMIZATION_SIZE", True)

        # Increase freertos tick speed from 100Hz to 1kHz so that delay() resolution is 1ms
        add_idf_sdkconfig_option("CONFIG_FREERTOS_HZ", 1000)

        # Setup watchdog
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT", True)
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_PANIC", True)
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0", False)
        add_idf_sdkconfig_option("CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1", False)

        # SDK Config defaults to match Arduino Framework:
        add_idf_sdkconfig_option("CONFIG_AUTOSTART_ARDUINO", True)
        add_idf_sdkconfig_option("CONFIG_ARDUINO_VARIANT", "esp32")
        add_idf_sdkconfig_option("CONFIG_ARDUHAL_ESP_LOG", True)

        add_idf_sdkconfig_option("CONFIG_COMPILER_STACK_CHECK_MODE_NORM", True)
        add_idf_sdkconfig_option("CONFIG_COMPILER_STACK_CHECK", True)
        add_idf_sdkconfig_option("CONFIG_COMPILER_WARN_WRITE_STRINGS", True)

        add_idf_sdkconfig_option("CONFIG_ESPTOOLPY_FLASHSIZE_4MB", True)
        add_idf_sdkconfig_option("CONFIG_ESPTOOLPY_FLASHSIZE", "4MB")

        add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)

        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODE_BTDM", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BLE_MAX_CONN", 3)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN", 2)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN", 0)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_PCM", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BR_EDR_SCO_DATA_PATH_EFF", 1)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PCM_ROLE_EDGE_CONFIG", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PCM_ROLE_MASTER", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PCM_POLAR_FALLING_EDGE", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PCM_ROLE_EFF", 0)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PCM_POLAR_EFF", 0)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_LEGACY_AUTH_VENDOR_EVT_EFF", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BLE_MAX_CONN_EFF", 3)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BR_EDR_MAX_ACL_CONN_EFF", 2)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_BR_EDR_MAX_SYNC_CONN_EFF", 0)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PINNED_TO_CORE_0", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_PINNED_TO_CORE", 0)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_HCI_MODE_VHCI", True)

        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODEM_SLEEP", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_MODEM_SLEEP_MODE_ORIG", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_LPCLK_SEL_MAIN_XTAL", True)

        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_DEFAULT_SCA_250PPM", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_SLEEP_CLOCK_ACCURACY_INDEX_EFF", 1)
        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_SCAN_DUPL", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_SCAN_DUPL_TYPE_DEVICE", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_SCAN_DUPL_TYPE", 0)
        add_idf_sdkconfig_option("CONFIG_BTDM_SCAN_DUPL_CACHE_SIZE", 20)
        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_MESH_SCAN_DUPL_EN", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_MESH_DUPL_SCAN_CACHE_SIZE", 100)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_FULL_SCAN_SUPPORTED", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_ADV_REPORT_FLOW_CTRL_SUPP", True)
        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_ADV_REPORT_FLOW_CTRL_NUM", 100)
        add_idf_sdkconfig_option("CONFIG_BTDM_BLE_ADV_REPORT_DISCARD_THRSHOLD", 20)
        add_idf_sdkconfig_option("CONFIG_BTDM_RESERVE_DRAM", 0xDB5C)
        add_idf_sdkconfig_option("CONFIG_BTDM_CTRL_HLI", True)

        add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_ENABLED", True)

        add_idf_sdkconfig_option("CONFIG_BT_BTC_TASK_STACK_SIZE", 8192)
        add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_PINNED_TO_CORE_0", True)
        add_idf_sdkconfig_option("CONFIG_BT_BLUEDROID_PINNED_TO_CORE", 0)
        add_idf_sdkconfig_option("CONFIG_BT_BTU_TASK_STACK_SIZE", 8192)
        add_idf_sdkconfig_option("CONFIG_BT_CLASSIC_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_A2DP_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_SPP_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_HFP_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_HFP_CLIENT_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_HFP_AUDIO_DATA_PATH_PCM", True)
        add_idf_sdkconfig_option("CONFIG_BT_SSP_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_BLE_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_BT_GATTS_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_GATT_MAX_SR_PROFILES", 8)
        add_idf_sdkconfig_option("CONFIG_BT_GATTS_SEND_SERVICE_CHANGE_AUTO", True)
        add_idf_sdkconfig_option("CONFIG_BT_GATTS_SEND_SERVICE_CHANGE_MODE", 0)
        add_idf_sdkconfig_option("CONFIG_BT_GATTC_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_GATTC_CONNECT_RETRY_COUNT", 3)
        add_idf_sdkconfig_option("CONFIG_BT_BLE_SMP_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_STACK_NO_LOG", True)
        add_idf_sdkconfig_option("CONFIG_BT_ACL_CONNECTIONS", 4)
        add_idf_sdkconfig_option("CONFIG_BT_MULTI_CONNECTION_ENBALE", True)
        add_idf_sdkconfig_option("CONFIG_BT_BLE_DYNAMIC_ENV_MEMORY", True)
        add_idf_sdkconfig_option("CONFIG_BT_SMP_ENABLE", True)
        add_idf_sdkconfig_option("CONFIG_BT_BLE_ESTAB_LINK_CONN_TOUT", 30)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_HCI_5_0", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_USE_DUPLICATE_SCAN", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_MEM_ALLOC_MODE_INTERNAL", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_DEINIT", True)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_PROV", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_PB_ADV", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_PROXY", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_NET_BUF_POOL_USAGE", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_SUBNET_COUNT", 3)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_APP_KEY_COUNT", 3)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_MODEL_KEY_COUNT", 3)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_MODEL_GROUP_COUNT", 3)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_LABEL_COUNT", 3)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_CRPL", 10)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_MSG_CACHE_SIZE", 10)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_ADV_BUF_COUNT", 60)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_IVU_DIVIDER", 4)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_TX_SEG_MSG_COUNT", 1)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_RX_SEG_MSG_COUNT", 1)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_RX_SDU_MAX", 384)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_TX_SEG_MAX", 32)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_TRACE_LEVEL_WARNING", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_STACK_TRACE_LEVEL", 2)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_NET_BUF_TRACE_LEVEL_WARNING", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_NET_BUF_TRACE_LEVEL", 2)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_CLIENT_MSG_TIMEOUT", 4000)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_HEALTH_SRV", True)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_GENERIC_SERVER", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_SENSOR_SERVER", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_TIME_SCENE_SERVER", True)
        add_idf_sdkconfig_option("CONFIG_BLE_MESH_LIGHTING_SERVER", True)

        add_idf_sdkconfig_option("CONFIG_BLE_MESH_DISCARD_OLD_SEQ_AUTH", True)

        for name, value in conf[CONF_SDKCONFIG_OPTIONS].items():
            add_idf_sdkconfig_option(name, RawSdkconfigValue(value))

        if conf[CONF_ADVANCED][CONF_IGNORE_EFUSE_MAC_CRC]:
            cg.add_define("USE_ESP32_IGNORE_EFUSE_MAC_CRC")
            add_idf_sdkconfig_option(
                "CONFIG_ESP32_PHY_CALIBRATION_AND_DATA_STORAGE", False
            )

        cg.add_define(
            "USE_ESP_IDF_VERSION_CODE",
            cg.RawExpression(
                f"VERSION_CODE({framework_ver.major}, {framework_ver.minor}, {framework_ver.patch})"
            ),
        )

        cg.add_define(
            "USE_ARDUINO_VERSION_CODE",
            cg.RawExpression(
                f"VERSION_CODE({framework_ver.major}, {framework_ver.minor}, {framework_ver.patch})"
            ),
        )


ARDUINO_PARTITIONS_CSV = """\
nvs,      data, nvs,     0x009000, 0x005000,
otadata,  data, ota,     0x00e000, 0x002000,
app0,     app,  ota_0,   0x010000, 0x1C0000,
app1,     app,  ota_1,   0x1D0000, 0x1C0000,
eeprom,   data, 0x99,    0x390000, 0x001000,
spiffs,   data, spiffs,  0x391000, 0x00F000
"""


IDF_PARTITIONS_CSV = """\
# Name,   Type, SubType, Offset,   Size, Flags
nvs,      data, nvs,     ,        0x4000,
otadata,  data, ota,     ,        0x2000,
phy_init, data, phy,     ,        0x1000,
app0,     app,  ota_0,   ,      0x1C0000,
app1,     app,  ota_1,   ,      0x1C0000,
"""


ARDUINO_IDF_PARTITIONS_CSV = """\
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x009000, 0x005000,
otadata,  data, ota,     0x00e000, 0x002000,
app0,     app,  ota_0,   0x010000, 0x200000,
app1,     app,  ota_1,   0x210000, 0x1F0000,
"""

ARDUINO_IDF_MD5_PATCH_PREFIX = """\
// >>> ARDUINO-IDF-FIX:
#ifdef USE_ARDUINO_IDF
#define USE_ESP_IDF
#undef USE_ARDUINO
#endif
// <<< ARDUINO-IDF-FIX
"""


def _format_sdkconfig_val(value: SdkconfigValueType) -> str:
    if isinstance(value, bool):
        return "y" if value else "n"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return f'"{value}"'
    if isinstance(value, RawSdkconfigValue):
        return value.value
    raise ValueError


def _write_sdkconfig():
    # sdkconfig.{name} stores the real sdkconfig (modified by esp-idf with default)
    # sdkconfig.{name}.esphomeinternal stores what esphome last wrote
    # we use the internal one to detect if there were any changes, and if so write them to the
    # real sdkconfig
    sdk_path = Path(CORE.relative_build_path(f"sdkconfig.{CORE.name}"))
    internal_path = Path(
        CORE.relative_build_path(f"sdkconfig.{CORE.name}.esphomeinternal")
    )

    want_opts = CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS]
    contents = (
        "\n".join(
            f"{name}={_format_sdkconfig_val(value)}"
            for name, value in sorted(want_opts.items())
        )
        + "\n"
    )
    if write_file_if_changed(internal_path, contents):
        # internal changed, update real one
        write_file_if_changed(sdk_path, contents)


# Called by writer.py
def copy_files():
    if CORE.data[KEY_CORE][REAL_TARGET_FRAMEWORK] == "arduino-idf":
        _write_sdkconfig()
        write_file_if_changed(
            CORE.relative_build_path("partitions.csv"),
            ARDUINO_IDF_PARTITIONS_CSV,
        )
        # IDF build scripts look for version string to put in the build.
        # However, if the build path does not have an initialized git repo,
        # and no version.txt file exists, the CMake script fails for some setups.
        # Fix by manually pasting a version.txt file, containing the ESPHome version
        write_file_if_changed(
            CORE.relative_build_path("version.txt"),
            __version__,
        )

        MD5_Cpp = CORE.relative_build_path("src/esphome/components/md5/md5.cpp")
        with open(MD5_Cpp) as original:
            md5_cpp_content = original.read()
        write_file_if_changed(MD5_Cpp, ARDUINO_IDF_MD5_PATCH_PREFIX + md5_cpp_content)

    elif CORE.using_arduino:
        write_file_if_changed(
            CORE.relative_build_path("partitions.csv"),
            ARDUINO_PARTITIONS_CSV,
        )
    elif CORE.using_esp_idf:
        _write_sdkconfig()
        write_file_if_changed(
            CORE.relative_build_path("partitions.csv"),
            IDF_PARTITIONS_CSV,
        )
        # IDF build scripts look for version string to put in the build.
        # However, if the build path does not have an initialized git repo,
        # and no version.txt file exists, the CMake script fails for some setups.
        # Fix by manually pasting a version.txt file, containing the ESPHome version
        write_file_if_changed(
            CORE.relative_build_path("version.txt"),
            __version__,
        )

    dir = os.path.dirname(__file__)
    post_build_file = os.path.join(dir, "post_build.py.script")
    copy_file_if_changed(
        post_build_file,
        CORE.relative_build_path("post_build.py"),
    )
