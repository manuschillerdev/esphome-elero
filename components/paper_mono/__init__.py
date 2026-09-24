"""M5Stack PaperMono board support, independent of the Elero frontend."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import esp32, i2c
from esphome.components.esp32.const import VARIANT_ESP32S3
from esphome.const import CONF_ADDRESS, CONF_ID, CONF_OUTPUT

DEPENDENCIES = ["i2c", "esp32"]
CONF_PAPER_MONO = "paper_mono"
ns = cg.esphome_ns.namespace("paper_mono")
PaperMono = ns.class_("PaperMono", cg.Component, i2c.I2CDevice)
RadioResetPin = ns.class_("RadioResetPin", cg.GPIOPin)

CONFIG_SCHEMA = cv.All(
    cv.Schema({cv.GenerateID(): cv.declare_id(PaperMono)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x4F))
    .extend({cv.Optional(CONF_ADDRESS, default=0x4F): cv.one_of(0x4F, int=True)}),
    esp32.only_on_variant(supported=[VARIANT_ESP32S3], msg_prefix="PaperMono"),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)


PIN_SCHEMA = pins.gpio_base_schema(
    RadioResetPin, cv.one_of(10, int=True), modes=[CONF_OUTPUT], mode_validator=lambda value: value
).extend({cv.Required(CONF_PAPER_MONO): cv.use_id(PaperMono)})


@pins.PIN_SCHEMA_REGISTRY.register(CONF_PAPER_MONO, PIN_SCHEMA)
async def reset_pin_to_code(config):
    if config.get("inverted", False):
        raise cv.Invalid("The PaperMono radio reset pin must not be inverted")
    var = cg.new_Pvariable(config[CONF_ID])
    parent = await cg.get_variable(config[CONF_PAPER_MONO])
    cg.add(var.set_parent(parent))
    return var
