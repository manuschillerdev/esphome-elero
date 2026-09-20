import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.core import CORE

from ..elero import CONF_ELERO_ID, CONF_REGISTRY_ID, DeviceRegistry, OutputAdapter, elero_ns

DEPENDENCIES = ["elero"]
AUTO_LOAD = ["json", "cover", "light"]
CODEOWNERS = ["@manuschillerdev"]

NvsAdapter = elero_ns.class_("NvsAdapter", cg.Component, OutputAdapter)
NvsLightState = elero_ns.class_("NvsLightState", cg.Component)
# Keep in sync with DeviceRegistry::MAX_DEVICES.
MAX_DEVICES = 48
_LIGHT_SLOT_IDS = [f"light_slot_{i}_id" for i in range(MAX_DEVICES)]

CONF_NVS_ADAPTER_ID = "nvs_adapter_id"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.GenerateID(CONF_REGISTRY_ID): cv.use_id(DeviceRegistry),
        cv.GenerateID(CONF_NVS_ADAPTER_ID): cv.declare_id(NvsAdapter),
        **{cv.GenerateID(key): cv.declare_id(NvsLightState) for key in _LIGHT_SLOT_IDS},
    }
)


async def to_code(config):
    registry = await cg.get_variable(config[CONF_REGISTRY_ID])

    # Enable NVS persistence on the unified device registry.
    cg.add(registry.set_nvs_enabled(True))

    # If elero_mqtt is loaded, it handles entity publishing via MQTT discovery.
    # Otherwise, create an NVS adapter that builds ESPHome entities from NVS at boot.
    if "elero_mqtt" not in CORE.loaded_integrations:
        cg.add(registry.set_hub_mode(cg.RawExpression("elero::HubMode::NATIVE")))

        # Ensure cover/light framework is enabled — ESPHome normally sets these
        # defines when it sees a `cover:`/`light:` block in YAML, but here entities
        # are created at runtime from NVS. Counts are pre-sized to MAX_DEVICES
        # since the actual count isn't known at codegen.
        cg.add_define("USE_COVER")
        cg.add_define("USE_LIGHT")
        for _ in range(MAX_DEVICES):
            CORE.register_platform_component("cover", None)
            CORE.register_platform_component("light", None)

        adapter = cg.new_Pvariable(config[CONF_NVS_ADAPTER_ID])
        cg.add(adapter.set_registry(registry))
        cg.add(registry.add_adapter(adapter))
        await cg.register_component(adapter, config)
        for index, key in enumerate(_LIGHT_SLOT_IDS):
            state = cg.new_Pvariable(config[key])
            cg.add(adapter.set_light_slot(index, state))
            await cg.register_component(state, {})
