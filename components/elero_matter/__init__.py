import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..elero import CONF_ELERO_ID, CONF_REGISTRY_ID, elero_ns, DeviceRegistry

DEPENDENCIES = ["elero"]
CODEOWNERS = ["@manuschillerdev"]

MatterAdapter = elero_ns.class_("MatterAdapter")

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MatterAdapter),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.GenerateID(CONF_REGISTRY_ID): cv.use_id(DeviceRegistry),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    # Register as output adapter on the unified registry
    registry = await cg.get_variable(config[CONF_REGISTRY_ID])
    cg.add(registry.add_adapter(var))
