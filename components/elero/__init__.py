import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID

DEPENDENCIES = ["spi"]

# x-release-please-version
ELERO_VERSION = "0.9.0"

elero_ns = cg.esphome_ns.namespace("elero")
elero = elero_ns.class_("Elero", spi.SPIDevice, cg.Component)

# New architecture: unified device registry
DeviceRegistry = elero_ns.class_("DeviceRegistry")

CONF_GDO0_PIN = "gdo0_pin"
CONF_ELERO_ID = "elero_id"
CONF_FREQ0 = "freq0"
CONF_FREQ1 = "freq1"
CONF_FREQ2 = "freq2"
CONF_REGISTRY_ID = "registry_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(elero),
            cv.GenerateID(CONF_REGISTRY_ID): cv.declare_id(DeviceRegistry),
            cv.Required(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_FREQ0, default=0x7A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ1, default=0x71): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ2, default=0x21): cv.hex_int_range(min=0x0, max=0xFF),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    gdo0_pin = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
    cg.add(var.set_gdo0_pin(gdo0_pin))
    cg.add(var.set_freq0(config[CONF_FREQ0]))
    cg.add(var.set_freq1(config[CONF_FREQ1]))
    cg.add(var.set_freq2(config[CONF_FREQ2]))

    cg.add(var.set_version(ELERO_VERSION))

    # Create device registry and wire to hub
    registry = cg.new_Pvariable(config[CONF_REGISTRY_ID])
    cg.add(registry.set_hub(var))
    cg.add(var.set_registry(registry))
