import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from .. import CONF_ELERO_ID, elero, elero_ns

DEPENDENCIES = ["elero"]

EleroScanButton = elero_ns.class_("EleroScanButton", button.Button, cg.Component)
# Reference to EleroLight class (resolved at code-gen time; avoids circular import)
EleroLight = elero_ns.class_("EleroLight")

CONF_SCAN_START = "scan_start"
CONF_LIGHT_ID = "light_id"
CONF_COMMAND_BYTE = "command_byte"

CONFIG_SCHEMA = (
    button.button_schema(EleroScanButton)
    .extend(
        {
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Optional(CONF_SCAN_START, default=True): cv.boolean,
            cv.Optional(CONF_LIGHT_ID): cv.use_id(EleroLight),
            cv.Optional(CONF_COMMAND_BYTE, default=0x44): cv.hex_int_range(min=0x0, max=0xFF),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(var.set_elero_parent(parent))
    cg.add(var.set_scan_start(config[CONF_SCAN_START]))

    if CONF_LIGHT_ID in config:
        light_var = await cg.get_variable(config[CONF_LIGHT_ID])
        cg.add(var.set_light(light_var))
        cg.add(var.set_command_byte(config[CONF_COMMAND_BYTE]))
