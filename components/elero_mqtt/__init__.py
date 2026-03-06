import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..elero import CONF_ELERO_ID, elero_ns

DEPENDENCIES = ["elero", "mqtt"]
CODEOWNERS = ["@manuschillerdev"]

# Classes
MqttDeviceManager = elero_ns.class_("MqttDeviceManager", cg.Component)
EleroDynamicCover = elero_ns.class_("EleroDynamicCover")
EleroDynamicLight = elero_ns.class_("EleroDynamicLight")
EleroRemoteControl = elero_ns.class_("EleroRemoteControl")

# Config keys
CONF_TOPIC_PREFIX = "topic_prefix"
CONF_DISCOVERY_PREFIX = "discovery_prefix"
CONF_MAX_COVERS = "max_covers"
CONF_MAX_LIGHTS = "max_lights"
CONF_MAX_REMOTES = "max_remotes"
CONF_DEVICE_NAME = "device_name"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MqttDeviceManager),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.Optional(CONF_TOPIC_PREFIX, default="elero"): cv.string,
        cv.Optional(CONF_DISCOVERY_PREFIX, default="homeassistant"): cv.string,
        cv.Optional(CONF_MAX_COVERS, default=16): cv.int_range(min=1, max=32),
        cv.Optional(CONF_MAX_LIGHTS, default=8): cv.int_range(min=1, max=32),
        cv.Optional(CONF_MAX_REMOTES, default=16): cv.int_range(min=1, max=32),
        cv.Optional(CONF_DEVICE_NAME, default="Elero Gateway"): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_ELERO_ID])

    max_covers = config[CONF_MAX_COVERS]
    max_lights = config[CONF_MAX_LIGHTS]
    max_remotes = config[CONF_MAX_REMOTES]

    # Pre-allocate dynamic slots as static arrays
    cg.add_global(
        cg.RawExpression(
            f"static esphome::elero::EleroDynamicCover elero_dynamic_covers[{max_covers}]"
        )
    )
    cg.add_global(
        cg.RawExpression(
            f"static esphome::elero::EleroDynamicLight elero_dynamic_lights[{max_lights}]"
        )
    )
    cg.add_global(
        cg.RawExpression(
            f"static esphome::elero::EleroRemoteControl elero_dynamic_remotes[{max_remotes}]"
        )
    )

    # Create the MqttDeviceManager component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_hub(hub))
    cg.add(var.set_topic_prefix(config[CONF_TOPIC_PREFIX]))
    cg.add(var.set_discovery_prefix(config[CONF_DISCOVERY_PREFIX]))
    cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))

    # Pass the pre-allocated slots
    cg.add(var.set_cover_slots(cg.RawExpression("elero_dynamic_covers"), max_covers))
    cg.add(var.set_light_slots(cg.RawExpression("elero_dynamic_lights"), max_lights))
    cg.add(var.set_remote_slots(cg.RawExpression("elero_dynamic_remotes"), max_remotes))
