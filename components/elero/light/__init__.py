import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import (
    CONF_CHANNEL,
    CONF_OUTPUT_ID,
)

from .. import CONF_ELERO_ID, elero, elero_ns

DEPENDENCIES = ["elero"]

CONF_DST_ADDRESS = "dst_address"
CONF_SRC_ADDRESS = "src_address"
CONF_PAYLOAD_1 = "payload_1"
CONF_PAYLOAD_2 = "payload_2"
CONF_TYPE = "type"
CONF_TYPE2 = "type2"
CONF_HOP = "hop"
CONF_DIM_DURATION = "dim_duration"

# New architecture: EspLightShell replaces EleroLight
EspLightShell = elero_ns.class_("EspLightShell", light.LightOutput, cg.Component)

CONFIG_SCHEMA = light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
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
    }
).extend(cv.COMPONENT_SCHEMA)


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
