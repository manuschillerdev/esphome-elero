import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID
from esphome.components.elero_web import (
    elero_ns,
    EleroWebServer,
    CONF_ELERO_WEB_ID,
)

DEPENDENCIES = ["elero_web"]

EleroWebSwitch = elero_ns.class_("EleroWebSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    switch.switch_schema(EleroWebSwitch)
    .extend(
        {
            cv.GenerateID(CONF_ELERO_WEB_ID): cv.use_id(EleroWebServer),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    web_server_var = await cg.get_variable(config[CONF_ELERO_WEB_ID])
    cg.add(var.set_web_server(web_server_var))
