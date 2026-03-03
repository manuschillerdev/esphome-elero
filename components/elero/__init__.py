"""
ESPHome component for Elero RF blinds and lights.

This is the core RF hub component. It handles:
- CC1101 RF transceiver communication via SPI
- Packet encoding/decoding and encryption
- Device registration and RF packet routing

Usage:
  elero:
    cs_pin: GPIO5
    gdo0_pin: GPIO26

    web:                    # Optional: Web UI at /elero
      port: 80

For MQTT mode with dynamic devices, add the separate elero_mqtt component.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID, CONF_PORT

DEPENDENCIES = ["spi", "network"]
AUTO_LOAD = ["elero_web"]  # Web UI is auto-loaded, MQTT is opt-in
CODEOWNERS = ["@manuschillerdev"]

# Namespace
elero_ns = cg.esphome_ns.namespace("elero")

# Classes
elero = elero_ns.class_("Elero", spi.SPIDevice, cg.Component)
EleroWebServer = elero_ns.class_("EleroWebServer", cg.Component)

# Config keys - hub
CONF_GDO0_PIN = "gdo0_pin"
CONF_ELERO_ID = "elero_id"
CONF_FREQ0 = "freq0"
CONF_FREQ1 = "freq1"
CONF_FREQ2 = "freq2"

# Config keys - web
CONF_WEB = "web"
CONF_WEB_ID = "web_id"

# Sub-schema for web UI
WEB_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_WEB_ID): cv.declare_id(EleroWebServer),
        cv.Optional(CONF_PORT, default=80): cv.port,
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(elero),
            cv.Required(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_FREQ0, default=0x7A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ1, default=0x71): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ2, default=0x21): cv.hex_int_range(min=0x0, max=0xFF),
            # Optional web UI
            cv.Optional(CONF_WEB): WEB_SCHEMA,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True))
)


async def to_code(config):
    # Create and register the hub
    hub = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(hub, config)
    await spi.register_spi_device(hub, config)

    # Configure RF settings
    gdo0_pin = await cg.gpio_pin_expression(config[CONF_GDO0_PIN])
    cg.add(hub.set_gdo0_pin(gdo0_pin))
    cg.add(hub.set_freq0(config[CONF_FREQ0]))
    cg.add(hub.set_freq1(config[CONF_FREQ1]))
    cg.add(hub.set_freq2(config[CONF_FREQ2]))

    # Web UI (optional)
    if CONF_WEB in config:
        web_config = config[CONF_WEB]

        # Create web server with declared ID (allows switch platform to reference it)
        web = cg.new_Pvariable(web_config[CONF_WEB_ID])
        await cg.register_component(web, {})

        cg.add(web.set_elero_parent(hub))
        cg.add(web.set_port(web_config[CONF_PORT]))
