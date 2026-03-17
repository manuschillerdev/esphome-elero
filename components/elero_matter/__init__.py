import logging
import os

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.const import CONF_ID
from esphome.core import CORE
from esphome.coroutine import coroutine_with_priority

from ..elero import CONF_ELERO_ID, CONF_REGISTRY_ID, DeviceRegistry, elero_ns

# ── Matter QR code computation (spec section 5.1.3) ──────────────────────────

_BASE38_CHARS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-."

# Verhoeff check digit tables
_VERHOEFF_D = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    [1, 2, 3, 4, 0, 6, 7, 8, 9, 5],
    [2, 3, 4, 0, 1, 7, 8, 9, 5, 6],
    [3, 4, 0, 1, 2, 8, 9, 5, 6, 7],
    [4, 0, 1, 2, 3, 9, 5, 6, 7, 8],
    [5, 9, 8, 7, 6, 0, 4, 3, 2, 1],
    [6, 5, 9, 8, 7, 1, 0, 4, 3, 2],
    [7, 6, 5, 9, 8, 2, 1, 0, 4, 3],
    [8, 7, 6, 5, 9, 3, 2, 1, 0, 4],
    [9, 8, 7, 6, 5, 4, 3, 2, 1, 0],
]
_VERHOEFF_INV = [0, 4, 3, 2, 1, 5, 6, 7, 8, 9]
_VERHOEFF_P = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9],
    [1, 5, 7, 6, 2, 8, 3, 0, 9, 4],
    [5, 8, 0, 3, 7, 9, 6, 1, 4, 2],
    [8, 9, 1, 6, 0, 4, 3, 5, 2, 7],
    [9, 4, 5, 3, 1, 2, 6, 8, 7, 0],
    [4, 2, 8, 6, 5, 7, 3, 9, 0, 1],
    [2, 7, 9, 3, 8, 0, 6, 4, 1, 5],
    [7, 0, 4, 6, 9, 1, 3, 2, 5, 8],
]


def _verhoeff_checksum(num_str: str) -> int:
    c = 0
    for i, ch in enumerate(reversed(num_str)):
        c = _VERHOEFF_D[c][_VERHOEFF_P[(i + 1) % 8][int(ch)]]
    return _VERHOEFF_INV[c]


def _base38_encode(data: bytes) -> str:
    result = []
    i = 0
    while i < len(data):
        remaining = len(data) - i
        if remaining >= 3:
            value = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16)
            for _ in range(5):
                result.append(_BASE38_CHARS[value % 38])
                value //= 38
            i += 3
        elif remaining == 2:
            value = data[i] | (data[i + 1] << 8)
            for _ in range(4):
                result.append(_BASE38_CHARS[value % 38])
                value //= 38
            i += 2
        else:
            value = data[i]
            for _ in range(2):
                result.append(_BASE38_CHARS[value % 38])
                value //= 38
            i += 1
    return "".join(result)


def _compute_qr_code(vid: int, pid: int, disc: int, passcode: int) -> str:
    """Compute Matter QR code setup payload (MT:... string)."""
    # Bit field layout (88 bits total, LSB first):
    # version(3) + VID(16) + PID(16) + flow(2) + capabilities(8) +
    # discriminator(12) + passcode(27) + padding(4)
    payload = 0
    payload |= 0 & 0x07  # version = 0
    payload |= (vid & 0xFFFF) << 3
    payload |= (pid & 0xFFFF) << 19
    payload |= 0 << 35  # flow = 0 (standard)
    payload |= 0x02 << 37  # capabilities = BLE
    payload |= (disc & 0xFFF) << 45
    payload |= (passcode & 0x7FFFFFF) << 57
    data = payload.to_bytes(11, byteorder="little")
    return "MT:" + _base38_encode(data)


def _compute_manual_code(disc: int, passcode: int) -> str:
    """Compute 11-digit manual pairing code with Verhoeff check digit."""
    # Short discriminator = top 4 bits of 12-bit discriminator
    short_disc = disc >> 8
    chunk1 = 0  # standard flow
    chunk2 = ((short_disc & 0x03) << 14) | (passcode & 0x3FFF)
    chunk3 = (passcode >> 14) & 0x1FFF
    code = f"{chunk1:01d}{chunk2:05d}{chunk3:04d}"
    return code + str(_verhoeff_checksum(code))


# ── Component definition ─────────────────────────────────────────────────────

_LOGGER = logging.getLogger(__name__)

DEPENDENCIES = ["elero"]
CODEOWNERS = ["@manuschillerdev"]

MatterAdapter = elero_ns.class_("MatterAdapter")

CONF_VENDOR_ID = "vendor_id"
CONF_PRODUCT_ID = "product_id"
CONF_DEVICE_NAME = "device_name"
CONF_DISCRIMINATOR = "discriminator"
CONF_PASSCODE = "passcode"


def _validate_esp_idf(config):
    if CORE.using_arduino:
        raise cv.Invalid(
            "elero_matter requires ESP-IDF framework. "
            "Set 'framework: type: esp-idf' in your esp32 config."
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MatterAdapter),
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
            cv.GenerateID(CONF_REGISTRY_ID): cv.use_id(DeviceRegistry),
            cv.Optional(CONF_VENDOR_ID, default=0xFFF1): cv.hex_uint16_t,
            cv.Optional(CONF_PRODUCT_ID, default=0x8000): cv.hex_uint16_t,
            cv.Optional(CONF_DEVICE_NAME, default="Elero Gateway"): cv.string,
            cv.Optional(CONF_DISCRIMINATOR, default=3840): cv.uint16_t,
            cv.Optional(CONF_PASSCODE, default=20202021): cv.uint32_t,
        }
    ),
    _validate_esp_idf,
)


# ── Post-FINAL coroutine: append MATTER_SDK_PATH to cmake_extra_args ─────────
# ESPHome's esp32 component sets cmake_extra_args to -DEXCLUDE_COMPONENTS=...
# at FINAL priority (-1000). We run after it to append our -DMATTER_SDK_PATH
# without overwriting the excludes.


@coroutine_with_priority(-1001.0)
async def _append_matter_cmake_args(matter_sdk_path, extra_component_dirs):
    existing = CORE.platformio_options.get("board_build.cmake_extra_args", "")
    CORE.platformio_options["board_build.cmake_extra_args"] = (
        f"-DMATTER_SDK_PATH={matter_sdk_path}"
        f" -DEXTRA_COMPONENT_DIRS={extra_component_dirs}"
        f" {existing}"
    )


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    cg.add(var.set_vendor_id(config[CONF_VENDOR_ID]))
    cg.add(var.set_product_id(config[CONF_PRODUCT_ID]))
    cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))
    cg.add(var.set_discriminator(config[CONF_DISCRIMINATOR]))
    cg.add(var.set_passcode(config[CONF_PASSCODE]))

    # Pre-compute commissioning payloads from YAML config
    qr_code = _compute_qr_code(
        config[CONF_VENDOR_ID],
        config[CONF_PRODUCT_ID],
        config[CONF_DISCRIMINATOR],
        config[CONF_PASSCODE],
    )
    manual_code = _compute_manual_code(
        config[CONF_DISCRIMINATOR],
        config[CONF_PASSCODE],
    )
    cg.add(var.set_qr_code(qr_code))
    cg.add(var.set_manual_code(manual_code))
    _LOGGER.info("Matter QR code: %s", qr_code)
    _LOGGER.info("Matter manual code: %s", manual_code)

    # Register as output adapter on the unified registry
    registry = await cg.get_variable(config[CONF_REGISTRY_ID])
    cg.add(registry.add_adapter(var))

    # Enable NVS persistence and set hub mode
    cg.add(registry.set_nvs_enabled(True))
    cg.add(registry.set_hub_mode(elero_ns.class_("HubMode").enum("MATTER")))

    # ── esp-matter SDK ──
    #
    # Matter requires the connectedhomeip (CHIP) SDK compiled from source.
    # No pre-built binaries exist — the data model is baked into the library.
    #
    # Setup (macOS — replace 'darwin' with 'linux' on Linux):
    #   cd <idf-path> && . ./export.sh && cd ..
    #   git clone --depth 1 https://github.com/espressif/esp-matter.git ../esp-matter
    #   cd ../esp-matter
    #   git submodule update --init --depth 1
    #   cd connectedhomeip/connectedhomeip
    #   ./scripts/checkout_submodules.py --platform esp32 darwin --shallow
    #   cd ../.. && ./install.sh && . ./export.sh

    esp_matter_path = os.environ.get("ESP_MATTER_PATH")

    if not esp_matter_path:
        raise cv.Invalid(
            "ESP_MATTER_PATH environment variable is required. "
            "Install the esp-matter SDK (macOS):\n"
            "  cd <idf-path> && . ./export.sh && cd ..\n"
            "  git clone --depth 1 https://github.com/espressif/esp-matter.git ../esp-matter\n"
            "  cd ../esp-matter && git submodule update --init --depth 1\n"
            "  cd connectedhomeip/connectedhomeip\n"
            "  ./scripts/checkout_submodules.py --platform esp32 darwin --shallow\n"
            "  cd ../.. && ./install.sh && . ./export.sh"
        )

    _LOGGER.info("Using esp-matter SDK: %s", esp_matter_path)

    matter_sdk_path = os.path.join(
        esp_matter_path, "connectedhomeip", "connectedhomeip"
    )

    # EXTRA_COMPONENT_DIRS: tells the IDF build system where to find esp_matter,
    # esp_matter_console, chip, and all other esp-matter sub-components.
    # MATTER_SDK_PATH: used by esp_matter's CMakeLists.txt for connectedhomeip
    # source/include dirs.
    # Both injected as cmake -D flags via cmake_extra_args, appended AFTER
    # esp32's EXCLUDE_COMPONENTS (priority -1001).
    extra_component_dirs = ";".join([
        os.path.join(esp_matter_path, "components"),
        os.path.join(matter_sdk_path, "config", "esp32", "components"),
    ])
    CORE.add_job(_append_matter_cmake_args, matter_sdk_path, extra_component_dirs)

    # ── sdkconfig for Matter ──

    # Device identity — wired from YAML config to sdkconfig (compile-time)
    add_idf_sdkconfig_option("CONFIG_DEVICE_VENDOR_ID", config[CONF_VENDOR_ID])
    add_idf_sdkconfig_option("CONFIG_DEVICE_PRODUCT_ID", config[CONF_PRODUCT_ID])
    add_idf_sdkconfig_option(
        "CONFIG_ESP_MATTER_DISCRIMINATOR", config[CONF_DISCRIMINATOR]
    )
    add_idf_sdkconfig_option(
        "CONFIG_ESP_MATTER_PASSCODE", config[CONF_PASSCODE]
    )

    # BLE for Matter commissioning
    add_idf_sdkconfig_option("CONFIG_BT_ENABLED", True)
    add_idf_sdkconfig_option("CONFIG_BT_NIMBLE_ENABLED", True)

    # Larger stacks for Matter/CHIP task
    add_idf_sdkconfig_option("CONFIG_ESP_MAIN_TASK_STACK_SIZE", 8192)
    add_idf_sdkconfig_option("CONFIG_CHIP_TASK_STACK_SIZE", 8192)

    # Disable CHIP interactive shell (saves flash)
    add_idf_sdkconfig_option("CONFIG_ENABLE_CHIP_SHELL", False)

    # Enable Matter data model
    add_idf_sdkconfig_option("CONFIG_ESP_MATTER_ENABLE_DATA_MODEL", True)
