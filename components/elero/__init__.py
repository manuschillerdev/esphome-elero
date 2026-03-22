import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import spi
from esphome.const import CONF_ID
from esphome.core import CORE

DEPENDENCIES = ["spi"]

# x-release-please-version
ELERO_VERSION = "0.9.0"

elero_ns = cg.esphome_ns.namespace("elero")
elero = elero_ns.class_("Elero", cg.Component)

# Radio drivers
CC1101Driver = elero_ns.class_("CC1101Driver", spi.SPIDevice)

# New architecture: unified device registry
DeviceRegistry = elero_ns.class_("DeviceRegistry")

CONF_GDO0_PIN = "gdo0_pin"
CONF_IRQ_PIN = "irq_pin"
CONF_ELERO_ID = "elero_id"
CONF_FREQ0 = "freq0"
CONF_FREQ1 = "freq1"
CONF_FREQ2 = "freq2"
CONF_REGISTRY_ID = "registry_id"
CONF_AUTO_STATS = "auto_stats"
CONF_RADIO = "radio"
CONF_DRIVER_ID = "driver_id"


def _validate_irq_pin(config):
    """Accept either irq_pin or gdo0_pin (backward compat)."""
    if CONF_IRQ_PIN not in config and CONF_GDO0_PIN not in config:
        raise cv.Invalid(f"Either '{CONF_IRQ_PIN}' or '{CONF_GDO0_PIN}' is required")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(elero),
            cv.GenerateID(CONF_REGISTRY_ID): cv.declare_id(DeviceRegistry),
            cv.GenerateID(CONF_DRIVER_ID): cv.declare_id(CC1101Driver),
            cv.Optional(CONF_RADIO, default="cc1101"): cv.one_of("cc1101", lower=True),
            cv.Optional(CONF_IRQ_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_FREQ0, default=0x7A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ1, default=0x71): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ2, default=0x21): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_AUTO_STATS, default=True): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True)),
    _validate_irq_pin,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Create CC1101 driver, register with SPI bus, wire to hub
    driver = cg.new_Pvariable(config[CONF_DRIVER_ID])
    await spi.register_spi_device(driver, config)
    cg.add(driver.set_freq0(config[CONF_FREQ0]))
    cg.add(driver.set_freq1(config[CONF_FREQ1]))
    cg.add(driver.set_freq2(config[CONF_FREQ2]))
    cg.add(var.set_driver(driver))

    # IRQ pin: prefer irq_pin, fall back to gdo0_pin for backward compat
    irq_pin_conf = config.get(CONF_IRQ_PIN, config.get(CONF_GDO0_PIN))
    irq_pin = await cg.gpio_pin_expression(irq_pin_conf)
    cg.add(var.set_irq_pin(irq_pin))

    # Frequency registers on hub (for get_freq0/1/2 accessors and reinit_frequency)
    cg.add(var.set_freq0(config[CONF_FREQ0]))
    cg.add(var.set_freq1(config[CONF_FREQ1]))
    cg.add(var.set_freq2(config[CONF_FREQ2]))

    cg.add(var.set_version(ELERO_VERSION))

    # Create device registry and wire to hub
    registry = cg.new_Pvariable(config[CONF_REGISTRY_ID])
    cg.add(registry.set_hub(var))
    cg.add(var.set_registry(registry))

    # Auto-create internal diagnostic sensors for RF stats
    # Requires sensor component to be loaded (e.g., via RSSI sensors or explicit `sensor:` in YAML)
    if config[CONF_AUTO_STATS] and "sensor" in CORE.loaded_integrations:
        from esphome.components import sensor

        # Import sensor namespace for C++ type reference
        sensor_ns = cg.esphome_ns.namespace("sensor")
        SensorClass = sensor_ns.class_("Sensor")

        stats_sensors = [
            ("tx_success_total", "Elero TX Success", "set_stats_tx_success_sensor"),
            ("tx_fail_total", "Elero TX Fail", "set_stats_tx_fail_sensor"),
            ("tx_recover_total", "Elero TX Recover", "set_stats_tx_recover_sensor"),
            ("rx_packets_total", "Elero RX Packets", "set_stats_rx_packets_sensor"),
            ("rx_drops_total", "Elero RX Drops", "set_stats_rx_drops_sensor"),
            ("fifo_overflow_total", "Elero FIFO Overflows", "set_stats_fifo_overflows_sensor"),
            ("watchdog_recovery_total", "Elero Watchdog Recoveries", "set_stats_watchdog_sensor"),
            ("dispatch_latency_us", "Elero Dispatch Latency", "set_stats_dispatch_latency_sensor"),
            ("queue_transit_us", "Elero Queue Transit", "set_stats_queue_transit_sensor"),
            ("last_rx_age_ms", "Elero Last RX Age", "set_stats_last_rx_age_sensor"),
        ]
        for sensor_id, name, setter in stats_sensors:
            sens_var_id = cv.declare_id(SensorClass)(f"elero_{sensor_id}")
            sens = cg.new_Pvariable(sens_var_id)
            cg.add(sens.set_name(name))
            cg.add(sens.set_internal(True))
            cg.add(sens.set_accuracy_decimals(0))
            cg.add(getattr(var, setter)(sens))
            cg.add(cg.App.register_sensor(sens))
