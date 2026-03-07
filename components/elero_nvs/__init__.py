import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..elero import CONF_ELERO_ID, elero_ns

DEPENDENCIES = ["elero"]
AUTO_LOAD = ["json", "cover", "light"]
CODEOWNERS = ["@manuschillerdev"]

# Classes
NativeNvsDeviceManager = elero_ns.class_("NativeNvsDeviceManager", cg.Component)
NativeNvsCover = elero_ns.class_("NativeNvsCover")
NativeNvsLight = elero_ns.class_("NativeNvsLight")
EleroRemoteControl = elero_ns.class_("EleroRemoteControl")

# Config keys
CONF_MAX_COVERS = "max_covers"
CONF_MAX_LIGHTS = "max_lights"
CONF_MAX_REMOTES = "max_remotes"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(NativeNvsDeviceManager),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.Optional(CONF_MAX_COVERS, default=16): cv.int_range(min=1, max=32),
        cv.Optional(CONF_MAX_LIGHTS, default=8): cv.int_range(min=1, max=32),
        cv.Optional(CONF_MAX_REMOTES, default=16): cv.int_range(min=1, max=32),
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
            f"static esphome::elero::NativeNvsCover elero_nvs_covers[{max_covers}]"
        )
    )
    cg.add_global(
        cg.RawExpression(
            f"static esphome::elero::NativeNvsLight elero_nvs_lights[{max_lights}]"
        )
    )
    cg.add_global(
        cg.RawExpression(
            f"static esphome::elero::EleroRemoteControl elero_nvs_remotes[{max_remotes}]"
        )
    )

    # Create the NativeNvsDeviceManager component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_hub(hub))

    # Pass the pre-allocated slots
    cg.add(var.set_cover_slots(cg.RawExpression("elero_nvs_covers"), max_covers))
    cg.add(var.set_light_slots(cg.RawExpression("elero_nvs_lights"), max_lights))
    cg.add(var.set_remote_slots(cg.RawExpression("elero_nvs_remotes"), max_remotes))

    # Ensure USE_COVER and USE_LIGHT are defined even without YAML entities.
    # NVS mode registers covers/lights at runtime from NVS, so ESPHome's codegen
    # won't see any entities — but we need App.register_cover/register_light.
    cg.add_define("USE_COVER")
    cg.add_define("USE_LIGHT")
