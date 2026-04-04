import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, cover, sensor, text_sensor
from esphome.const import (
    CONF_CHANNEL,
    CONF_CLOSE_DURATION,
    CONF_NAME,
    CONF_OPEN_DURATION,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
)

from .. import CONF_ELERO_ID, elero, elero_ns

DEPENDENCIES = ["elero"]
CODEOWNERS = ["@andyboeh"]
AUTO_LOAD = ["binary_sensor", "sensor", "text_sensor"]

CONF_DST_ADDRESS = "dst_address"
CONF_SRC_ADDRESS = "src_address"
CONF_PAYLOAD_1 = "payload_1"
CONF_PAYLOAD_2 = "payload_2"
CONF_TYPE = "type"
CONF_TYPE2 = "type2"
CONF_HOP = "hop"
CONF_COMMAND_UP = "command_up"
CONF_COMMAND_DOWN = "command_down"
CONF_COMMAND_STOP = "command_stop"
CONF_COMMAND_CHECK = "command_check"
CONF_COMMAND_TILT = "command_tilt"
CONF_SUPPORTS_TILT = "supports_tilt"
CONF_AUTO_SENSORS = "auto_sensors"
CONF_RSSI_SENSOR = "rssi_sensor"
CONF_STATUS_SENSOR = "status_sensor"
CONF_PROBLEM_SENSOR = "problem_sensor"
CONF_COMMAND_SOURCE_SENSOR = "command_source_sensor"
CONF_PROBLEM_TYPE_SENSOR = "problem_type_sensor"
CONF_DEVICE_CLASS = "device_class"

# New architecture: EspCoverShell replaces EleroCover
EspCoverShell = elero_ns.class_("EspCoverShell", cover.Cover, cg.Component)

_RSSI_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
    state_class=STATE_CLASS_MEASUREMENT,
)
_STATUS_SENSOR_SCHEMA = text_sensor.text_sensor_schema()
_PROBLEM_SENSOR_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class="problem",
)
_COMMAND_SOURCE_SENSOR_SCHEMA = text_sensor.text_sensor_schema(
    entity_category="diagnostic",
    icon="mdi:remote",
)
_PROBLEM_TYPE_SENSOR_SCHEMA = text_sensor.text_sensor_schema(
    entity_category="diagnostic",
    icon="mdi:alert-circle-outline",
)



def _validate_duration_consistency(config):
    """Validate that if position tracking is enabled, both open and close durations are set."""
    open_dur = config.get(CONF_OPEN_DURATION)
    close_dur = config.get(CONF_CLOSE_DURATION)

    open_ms = open_dur.total_milliseconds if open_dur is not None else 0
    close_ms = close_dur.total_milliseconds if close_dur is not None else 0

    if open_ms == 0 and close_ms == 0:
        return config
    if open_ms > 0 and close_ms > 0:
        return config

    raise cv.Invalid(
        f"Position tracking requires both open_duration and close_duration to be set. "
        f"Current values: open_duration={open_ms}ms, close_duration={close_ms}ms. "
        f"Either set both to 0 (disable tracking) or both to non-zero values."
    )


def _auto_sensor_validator(config):
    """At validation time, inject auto-sensor sub-configs when auto_sensors=True."""
    if not config.get(CONF_AUTO_SENSORS, True):
        return config
    cover_name = config.get(CONF_NAME, "Elero Cover")
    result = dict(config)
    if CONF_RSSI_SENSOR not in result:
        result[CONF_RSSI_SENSOR] = _RSSI_SENSOR_SCHEMA({CONF_NAME: f"{cover_name} RSSI"})
    if CONF_STATUS_SENSOR not in result:
        result[CONF_STATUS_SENSOR] = _STATUS_SENSOR_SCHEMA({CONF_NAME: f"{cover_name} Status"})
    if CONF_PROBLEM_SENSOR not in result:
        result[CONF_PROBLEM_SENSOR] = _PROBLEM_SENSOR_SCHEMA({CONF_NAME: f"{cover_name} Problem"})
    if CONF_COMMAND_SOURCE_SENSOR not in result:
        result[CONF_COMMAND_SOURCE_SENSOR] = _COMMAND_SOURCE_SENSOR_SCHEMA({CONF_NAME: f"{cover_name} Command Source"})
    if CONF_PROBLEM_TYPE_SENSOR not in result:
        result[CONF_PROBLEM_TYPE_SENSOR] = _PROBLEM_TYPE_SENSOR_SCHEMA({CONF_NAME: f"{cover_name} Problem Type"})
    return result


CONFIG_SCHEMA = cv.All(
    cover.cover_schema(EspCoverShell)
    .extend(
        {
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Required(CONF_DST_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
            cv.Required(CONF_CHANNEL): cv.int_range(min=0, max=255),
            cv.Required(CONF_SRC_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
            cv.Optional(CONF_OPEN_DURATION, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_CLOSE_DURATION, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PAYLOAD_1, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_PAYLOAD_2, default=0x04): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_TYPE, default=0x6A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_TYPE2, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_HOP, default=0x0A): cv.hex_int_range(min=0x0, max=0xFF),
            # Legacy: command overrides (accepted for backward compat, not used by new arch)
            cv.Optional(CONF_COMMAND_UP, default=0x20): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_DOWN, default=0x40): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_STOP, default=0x10): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_CHECK, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_TILT, default=0x24): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_SUPPORTS_TILT, default=False): cv.boolean,
            cv.Optional(CONF_DEVICE_CLASS, default="shutter"): cv.one_of(
                "shutter", "blind", "awning", "curtain", "shade", "garage", lower=True
            ),
            cv.Optional(CONF_AUTO_SENSORS, default=True): cv.boolean,
            cv.Optional(CONF_RSSI_SENSOR): _RSSI_SENSOR_SCHEMA,
            cv.Optional(CONF_STATUS_SENSOR): _STATUS_SENSOR_SCHEMA,
            cv.Optional(CONF_PROBLEM_SENSOR): _PROBLEM_SENSOR_SCHEMA,
            cv.Optional(CONF_COMMAND_SOURCE_SENSOR): _COMMAND_SOURCE_SENSOR_SCHEMA,
            cv.Optional(CONF_PROBLEM_TYPE_SENSOR): _PROBLEM_TYPE_SENSOR_SCHEMA,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _validate_duration_consistency,
    _auto_sensor_validator,
)


async def to_code(config):
    var = await cover.new_cover(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_ELERO_ID])

    # Wire to device registry (created by hub)
    cg.add(var.set_registry(parent.get_registry()))

    # Set device config (shell registers with registry during setup)
    cg.add(var.set_dst_address(config[CONF_DST_ADDRESS]))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_src_address(config[CONF_SRC_ADDRESS]))
    cg.add(var.set_open_duration(config[CONF_OPEN_DURATION]))
    cg.add(var.set_close_duration(config[CONF_CLOSE_DURATION]))
    cg.add(var.set_payload_1(config[CONF_PAYLOAD_1]))
    cg.add(var.set_payload_2(config[CONF_PAYLOAD_2]))
    cg.add(var.set_type(config[CONF_TYPE]))
    cg.add(var.set_type2(config[CONF_TYPE2]))
    cg.add(var.set_hop(config[CONF_HOP]))
    cg.add(var.set_supports_tilt(config[CONF_SUPPORTS_TILT]))

    # Device class for HA — values must match HaCoverClass enum in device_type.h
    device_class_map = {
        "shutter": 0, "blind": 1, "awning": 2,
        "curtain": 3, "shade": 4, "garage": 5,
    }
    cg.add(var.set_ha_device_class(device_class_map.get(config[CONF_DEVICE_CLASS], 0)))

    # All sensors registered with the shell (published from snapshot in sync_and_publish_)
    if CONF_RSSI_SENSOR in config:
        rssi_var = await sensor.new_sensor(config[CONF_RSSI_SENSOR])
        cg.add(var.set_rssi_sensor(rssi_var))

    if CONF_STATUS_SENSOR in config:
        status_var = await text_sensor.new_text_sensor(config[CONF_STATUS_SENSOR])
        cg.add(var.set_status_sensor(status_var))

    if CONF_PROBLEM_SENSOR in config:
        problem_var = await binary_sensor.new_binary_sensor(config[CONF_PROBLEM_SENSOR])
        cg.add(var.set_problem_sensor(problem_var))

    if CONF_COMMAND_SOURCE_SENSOR in config:
        cs_var = await text_sensor.new_text_sensor(config[CONF_COMMAND_SOURCE_SENSOR])
        cg.add(var.set_command_source_sensor(cs_var))

    if CONF_PROBLEM_TYPE_SENSOR in config:
        pt_var = await text_sensor.new_text_sensor(config[CONF_PROBLEM_TYPE_SENSOR])
        cg.add(var.set_problem_type_sensor(pt_var))



