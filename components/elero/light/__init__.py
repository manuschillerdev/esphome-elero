import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import (
    CONF_CHANNEL,
    CONF_OUTPUT_ID,
)
from .. import elero_ns, elero, CONF_ELERO_ID

DEPENDENCIES = ["elero"]

CONF_BLIND_ADDRESS = "blind_address"
CONF_REMOTE_ADDRESS = "remote_address"
CONF_PAYLOAD_1 = "payload_1"
CONF_PAYLOAD_2 = "payload_2"
CONF_PCKINF_1 = "pck_inf1"
CONF_PCKINF_2 = "pck_inf2"
CONF_HOP = "hop"
CONF_DIM_DURATION = "dim_duration"
CONF_COMMAND_ON = "command_on"
CONF_COMMAND_OFF = "command_off"
CONF_COMMAND_DIM_UP = "command_dim_up"
CONF_COMMAND_DIM_DOWN = "command_dim_down"
CONF_COMMAND_STOP = "command_stop"
CONF_COMMAND_CHECK = "command_check"

EleroLight = elero_ns.class_("EleroLight", light.LightOutput, cg.Component)

CONFIG_SCHEMA = (
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(EleroLight),
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Required(CONF_BLIND_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
            cv.Required(CONF_CHANNEL): cv.int_range(min=0, max=255),
            cv.Required(CONF_REMOTE_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
            cv.Optional(CONF_DIM_DURATION, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_PAYLOAD_1, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_PAYLOAD_2, default=0x04): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_PCKINF_1, default=0x6A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_PCKINF_2, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_HOP, default=0x0A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_ON, default=0x20): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_OFF, default=0x40): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_DIM_UP, default=0x20): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_DIM_DOWN, default=0x40): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_STOP, default=0x10): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_COMMAND_CHECK, default=0x00): cv.hex_int_range(min=0x0, max=0xFF),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(var.set_elero_parent(parent))
    cg.add(var.set_blind_address(config[CONF_BLIND_ADDRESS]))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_remote_address(config[CONF_REMOTE_ADDRESS]))
    cg.add(var.set_dim_duration(config[CONF_DIM_DURATION]))
    cg.add(var.set_payload_1(config[CONF_PAYLOAD_1]))
    cg.add(var.set_payload_2(config[CONF_PAYLOAD_2]))
    cg.add(var.set_pckinf_1(config[CONF_PCKINF_1]))
    cg.add(var.set_pckinf_2(config[CONF_PCKINF_2]))
    cg.add(var.set_hop(config[CONF_HOP]))
    cg.add(var.set_command_on(config[CONF_COMMAND_ON]))
    cg.add(var.set_command_off(config[CONF_COMMAND_OFF]))
    cg.add(var.set_command_dim_up(config[CONF_COMMAND_DIM_UP]))
    cg.add(var.set_command_dim_down(config[CONF_COMMAND_DIM_DOWN]))
    cg.add(var.set_command_stop(config[CONF_COMMAND_STOP]))
    cg.add(var.set_command_check(config[CONF_COMMAND_CHECK]))
