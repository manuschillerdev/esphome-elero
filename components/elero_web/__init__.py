import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.components.elero import CONF_ELERO_ID, elero, elero_ns
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID
from esphome.core import CORE

DEPENDENCIES = ["elero", "web_server_base"]
AUTO_LOAD = ["web_server_base"]
CODEOWNERS = ["@manuschillerdev"]

# Exported so the switch sub-platform can reference the web server class
CONF_ELERO_WEB_ID = "elero_web_id"
EleroWebServer = elero_ns.class_("EleroWebServer", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EleroWebServer),
        cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_ELERO_ID])
    cg.add(var.set_elero_parent(parent))

    web_server_base_var = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_server(web_server_base_var))

    # Add ESPAsyncWebServer + AsyncTCP for Arduino builds
    # Must match versions used by ESPHome's web_server_base and async_tcp components
    if CORE.using_arduino and CORE.is_esp32:
        cg.add_library("ESP32Async/AsyncTCP", "3.4.5")
        cg.add_library("ESP32Async/ESPAsyncWebServer", "3.7.10")

