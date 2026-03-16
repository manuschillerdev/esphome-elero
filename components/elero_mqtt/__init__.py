import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..elero import CONF_ELERO_ID, CONF_REGISTRY_ID, DeviceRegistry, elero_ns

DEPENDENCIES = ["elero", "mqtt"]
AUTO_LOAD = ["json"]
CODEOWNERS = ["@manuschillerdev"]

MqttAdapter = elero_ns.class_("MqttAdapter")

# Config keys
CONF_TOPIC_PREFIX = "topic_prefix"
CONF_DISCOVERY_PREFIX = "discovery_prefix"
CONF_MAX_DEVICES = "max_devices"
CONF_DEVICE_NAME = "device_name"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MqttAdapter),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero_ns.class_("Elero")),
        cv.GenerateID(CONF_REGISTRY_ID): cv.use_id(DeviceRegistry),
        cv.Optional(CONF_TOPIC_PREFIX, default="elero"): cv.string,
        cv.Optional(CONF_DISCOVERY_PREFIX, default="homeassistant"): cv.string,
        cv.Optional(CONF_DEVICE_NAME, default="Elero Gateway"): cv.string,
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    # Configure MQTT adapter
    cg.add(var.set_topic_prefix(config[CONF_TOPIC_PREFIX]))
    cg.add(var.set_discovery_prefix(config[CONF_DISCOVERY_PREFIX]))
    cg.add(var.set_device_name(config[CONF_DEVICE_NAME]))

    # Register as output adapter on the unified registry
    registry = await cg.get_variable(config[CONF_REGISTRY_ID])
    cg.add(registry.add_adapter(var))

    # Enable NVS persistence on the registry (MQTT mode manages devices at runtime)
    cg.add(registry.set_nvs_enabled(True))
