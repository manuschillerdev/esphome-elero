"""PaperMono native management frontend using the existing Elero core."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import display, font, spi
from esphome.const import CONF_BUSY_PIN, CONF_DC_PIN, CONF_ID
from esphome.core import CoroPriority, coroutine_with_priority

from ..elero import CONF_ELERO_ID, CONF_REGISTRY_ID, DeviceRegistry, elero
from ..paper_mono import PaperMono

DEPENDENCIES = ["elero", "paper_mono", "spi"]


def AUTO_LOAD(config):
    dependencies = ["paper_mono_display", "font"]
    if config.get("storage", False):
        dependencies.extend(["paper_mono_storage", "elero_config"])
    return dependencies


ns = cg.esphome_ns.namespace("elero_paper")
PaperFrontend = ns.class_("PaperFrontend", display.DisplayBuffer, spi.SPIDevice)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PaperFrontend),
            cv.GenerateID(CONF_REGISTRY_ID): cv.use_id(DeviceRegistry),
            cv.GenerateID(CONF_ELERO_ID): cv.use_id(elero),
            cv.Optional("storage", default=False): cv.boolean,
            cv.Required("board_id"): cv.use_id(PaperMono),
            cv.Required("font_id"): cv.use_id(font.Font),
            cv.Required("title_font_id"): cv.use_id(font.Font),
            cv.Required(CONF_DC_PIN): pins.internal_gpio_output_pin_schema,
            cv.Required(CONF_BUSY_PIN): pins.internal_gpio_input_pin_schema,
        }
    )
    .extend(cv.polling_component_schema("never"))
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


@coroutine_with_priority(CoroPriority.BUS)
async def to_code(config):
    if config["storage"]:
        cg.add_define("USE_ELERO_PAPER_STORAGE")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config, write_only=True)
    cg.add(var.set_board(await cg.get_variable(config["board_id"])))
    cg.add(var.set_font(await cg.get_variable(config["font_id"])))
    cg.add(var.set_title_font(await cg.get_variable(config["title_font_id"])))
    cg.add(var.set_dc_pin(await cg.gpio_pin_expression(config[CONF_DC_PIN])))
    cg.add(var.set_busy_pin(await cg.gpio_pin_expression(config[CONF_BUSY_PIN])))
    registry = await cg.get_variable(config[CONF_REGISTRY_ID])
    cg.add(var.set_hub(await cg.get_variable(config[CONF_ELERO_ID])))
    cg.add(registry.set_nvs_enabled(True))
    cg.add(registry.set_receiver_discovery_enabled(True))
    cg.add(registry.add_adapter(var.get_adapter()))
