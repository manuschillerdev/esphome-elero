import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    DEVICE_CLASS_SIGNAL_STRENGTH,
    STATE_CLASS_MEASUREMENT,
    UNIT_DECIBEL_MILLIWATT,
)

from .. import CONF_ELERO_ID, elero

DEPENDENCIES = ["elero"]

CONF_BLIND_ADDRESS = "blind_address"

CONFIG_SCHEMA = (
    sensor.sensor_schema(
        unit_of_measurement=UNIT_DECIBEL_MILLIWATT,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_SIGNAL_STRENGTH,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Required(CONF_BLIND_ADDRESS): cv.hex_int_range(min=0x0, max=0xFFFFFF),
        }
    )
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(parent.register_rssi_sensor(config[CONF_BLIND_ADDRESS], var))
