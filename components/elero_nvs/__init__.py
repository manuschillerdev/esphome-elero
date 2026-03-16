import esphome.codegen as cg
import esphome.config_validation as cv

from ..elero import CONF_ELERO_ID, CONF_REGISTRY_ID, DeviceRegistry, elero_ns

DEPENDENCIES = ["elero"]
AUTO_LOAD = ["json"]
CODEOWNERS = ["@manuschillerdev"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.GenerateID(CONF_REGISTRY_ID): cv.use_id(DeviceRegistry),
    }
)


async def to_code(config):
    # Enable NVS persistence on the unified device registry.
    # Devices are managed at runtime via the web UI CRUD API and persisted in NVS.
    # On boot, active devices are restored from NVS into the registry.
    registry = await cg.get_variable(config[CONF_REGISTRY_ID])
    cg.add(registry.set_nvs_enabled(True))
