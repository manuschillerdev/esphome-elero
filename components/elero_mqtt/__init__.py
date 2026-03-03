"""
Elero MQTT mode component for dynamic device management.

This component enables MQTT-based device discovery and management:
- Devices are stored in NVS (persist across reboots)
- MQTT discovery payloads are published to Home Assistant
- Runtime add/remove/update of devices via web UI

Usage:
    mqtt:
      broker: 192.168.1.100

    elero:
      cs_pin: GPIO5
      gdo0_pin: GPIO26

    elero_mqtt:                   # Enables MQTT mode
      topic_prefix: elero         # MQTT topic prefix
      discovery_prefix: homeassistant
      max_covers: 16              # Pre-allocated cover slots
      max_lights: 8               # Pre-allocated light slots

When this component is present, the elero hub operates in MQTT mode.
Native mode (YAML-defined devices) is used when this component is absent.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

# Import from parent elero component for shared namespace
from ..elero import elero_ns, CONF_ELERO_ID

DEPENDENCIES = ["elero", "mqtt"]
CODEOWNERS = ["@manuschillerdev"]

# Classes
MqttDeviceManager = elero_ns.class_("MqttDeviceManager", cg.Component)
EleroDynamicCover = elero_ns.class_("EleroDynamicCover", cg.Component)
EleroDynamicLight = elero_ns.class_("EleroDynamicLight", cg.Component)

# Config keys
CONF_TOPIC_PREFIX = "topic_prefix"
CONF_DISCOVERY_PREFIX = "discovery_prefix"
CONF_MAX_COVERS = "max_covers"
CONF_MAX_LIGHTS = "max_lights"
CONF_DEVICE_NAME = "device_name"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MqttDeviceManager),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.Optional(CONF_TOPIC_PREFIX, default="elero"): cv.string,
        cv.Optional(CONF_DISCOVERY_PREFIX, default="homeassistant"): cv.string,
        cv.Optional(CONF_MAX_COVERS, default=16): cv.int_range(min=1, max=32),
        cv.Optional(CONF_MAX_LIGHTS, default=8): cv.int_range(min=1, max=32),
        cv.Optional(CONF_DEVICE_NAME, default="Elero Gateway"): cv.string,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    # Get the elero hub
    hub = await cg.get_variable(config[CONF_ELERO_ID])

    # ArduinoJson for MQTT discovery payloads
    cg.add_library("bblanchon/ArduinoJson", "7.4.2")

    max_covers = config[CONF_MAX_COVERS]
    max_lights = config[CONF_MAX_LIGHTS]

    # Pre-allocate dynamic cover/light slots as static arrays
    # These are simple declarations, acceptable as RawExpression
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

    # Create the MqttDeviceManager component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Configure via setters (clean codegen, no inline C++)
    cg.add(var.set_hub(hub))
    cg.add(var.set_topic_prefix(config[CONF_TOPIC_PREFIX]))
    cg.add(var.set_discovery_prefix(config[CONF_DISCOVERY_PREFIX]))
    cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))

    # Pass the pre-allocated slots
    cg.add(
        var.set_cover_slots(cg.RawExpression("elero_dynamic_covers"), max_covers)
    )
    cg.add(
        var.set_light_slots(cg.RawExpression("elero_dynamic_lights"), max_lights)
    )
