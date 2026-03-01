import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.elero import CONF_ELERO_ID, elero, elero_ns
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["elero", "network", "logger"]
CODEOWNERS = ["@manuschillerdev"]

# Exported so the switch sub-platform can reference the web server class
CONF_ELERO_WEB_ID = "elero_web_id"
EleroWebServer = elero_ns.class_("EleroWebServer", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EleroWebServer),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
        cv.Optional(CONF_PORT, default=80): cv.port,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(var.set_elero_parent(parent))
    cg.add(var.set_port(config[CONF_PORT]))
