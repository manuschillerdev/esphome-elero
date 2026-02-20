import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID
from .. import elero_ns, elero, CONF_ELERO_ID

DEPENDENCIES = ["elero"]

EleroScanButton = elero_ns.class_("EleroScanButton", button.Button, cg.Component)

CONF_SCAN_START = "scan_start"

CONFIG_SCHEMA = (
    button.button_schema(EleroScanButton)
    .extend(
        {
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Optional(CONF_SCAN_START, default=True): cv.boolean,
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
