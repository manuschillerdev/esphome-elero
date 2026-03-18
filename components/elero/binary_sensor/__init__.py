import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor

from .. import CONF_ELERO_ID, elero

DEPENDENCIES = ["elero"]

CONF_DST_ADDRESS = "dst_address"

CONFIG_SCHEMA = binary_sensor.binary_sensor_schema(
    device_class="problem",
).extend(
    {
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
        cv.Required(CONF_DST_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
    }
)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(parent.register_problem_sensor(config[CONF_DST_ADDRESS], var))
