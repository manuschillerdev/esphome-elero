import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor

from .. import CONF_ELERO_ID, elero

DEPENDENCIES = ["elero"]

CONF_BLIND_ADDRESS = "blind_address"

CONFIG_SCHEMA = text_sensor.text_sensor_schema().extend(
    {
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
        cv.Required(CONF_BLIND_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
    }
)


async def to_code(config):
    var = await text_sensor.new_text_sensor(config)
    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(parent.register_text_sensor(config[CONF_BLIND_ADDRESS], var))
