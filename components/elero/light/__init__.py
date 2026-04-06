import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button, light, sensor, text_sensor
from esphome.const import (
    CONF_CHANNEL,
    CONF_NAME,
    CONF_OUTPUT_ID,
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
)

from .. import CONF_ELERO_ID, elero, elero_ns

DEPENDENCIES = ["elero"]
AUTO_LOAD = ["button", "sensor", "text_sensor"]

CONF_DST_ADDRESS = "dst_address"
CONF_SRC_ADDRESS = "src_address"
CONF_PAYLOAD_1 = "payload_1"
CONF_PAYLOAD_2 = "payload_2"
CONF_TYPE = "type"
CONF_TYPE2 = "type2"
CONF_HOP = "hop"
CONF_DIM_DURATION = "dim_duration"
CONF_AUTO_SENSORS = "auto_sensors"
CONF_RSSI_SENSOR = "rssi_sensor"
CONF_STATUS_SENSOR = "status_sensor"
CONF_REFRESH_BUTTON = "refresh_button"

# New architecture: EspLightShell replaces EleroLight
EspLightShell = elero_ns.class_("EspLightShell", light.LightOutput, cg.Component)
RefreshButton = elero_ns.class_("RefreshButton", button.Button)

_RSSI_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
    state_class=STATE_CLASS_MEASUREMENT,
)
_STATUS_SENSOR_SCHEMA = text_sensor.text_sensor_schema()
_REFRESH_BUTTON_SCHEMA = button.button_schema(
    RefreshButton,
    entity_category="diagnostic",
    icon="mdi:refresh",
)


def _auto_sensor_validator(config):
    """At validation time, inject auto-sensor sub-configs when auto_sensors=True."""
    if not config.get(CONF_AUTO_SENSORS, True):
        return config
    light_name = config.get(CONF_NAME, "Elero Light")
    result = dict(config)
    if CONF_RSSI_SENSOR not in result:
        result[CONF_RSSI_SENSOR] = _RSSI_SENSOR_SCHEMA({CONF_NAME: f"{light_name} RSSI"})
    if CONF_STATUS_SENSOR not in result:
        result[CONF_STATUS_SENSOR] = _STATUS_SENSOR_SCHEMA({CONF_NAME: f"{light_name} Status"})
    if CONF_REFRESH_BUTTON not in result:
        result[CONF_REFRESH_BUTTON] = _REFRESH_BUTTON_SCHEMA({CONF_NAME: f"{light_name} Refresh"})
    return result


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(EspLightShell),
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Required(CONF_DST_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
            cv.Required(CONF_CHANNEL): cv.int_range(min=0, max=255),
            cv.Required(CONF_SRC_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
            cv.Optional(CONF_DIM_DURATION, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PAYLOAD_1, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_PAYLOAD_2, default=0x04): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_TYPE, default=0x6A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_TYPE2, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_HOP, default=0x0A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_AUTO_SENSORS, default=True): cv.boolean,
            cv.Optional(CONF_RSSI_SENSOR): _RSSI_SENSOR_SCHEMA,
            cv.Optional(CONF_STATUS_SENSOR): _STATUS_SENSOR_SCHEMA,
            cv.Optional(CONF_REFRESH_BUTTON): _REFRESH_BUTTON_SCHEMA,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _auto_sensor_validator,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    parent = await cg.get_variable(config[CONF_ELERO_ID])

    # Wire to device registry (created by hub)
    cg.add(var.set_registry(parent.get_registry()))

    # Set device config (shell registers with registry during setup)
    cg.add(var.set_dst_address(config[CONF_DST_ADDRESS]))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_src_address(config[CONF_SRC_ADDRESS]))
    cg.add(var.set_dim_duration(config[CONF_DIM_DURATION]))
    cg.add(var.set_payload_1(config[CONF_PAYLOAD_1]))
    cg.add(var.set_payload_2(config[CONF_PAYLOAD_2]))
    cg.add(var.set_type(config[CONF_TYPE]))
    cg.add(var.set_type2(config[CONF_TYPE2]))
    cg.add(var.set_hop(config[CONF_HOP]))

    # LightOutput doesn't inherit EntityBase, so pass name explicitly
    if "name" in config:
        cg.add(var.set_device_name(config["name"]))

    # All sensors registered with the shell (published from snapshot in sync_and_publish_)
    if CONF_RSSI_SENSOR in config:
        rssi_var = await sensor.new_sensor(config[CONF_RSSI_SENSOR])
        cg.add(var.set_rssi_sensor(rssi_var))

    if CONF_STATUS_SENSOR in config:
        status_var = await text_sensor.new_text_sensor(config[CONF_STATUS_SENSOR])
        cg.add(var.set_status_sensor(status_var))

    if CONF_REFRESH_BUTTON in config:
        btn_var = await button.new_button(config[CONF_REFRESH_BUTTON])
        cg.add(var.set_refresh_button(btn_var))
