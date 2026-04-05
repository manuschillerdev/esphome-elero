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
Sx1262Driver = elero_ns.class_("Sx1262Driver", spi.SPIDevice)
Sx1276Driver = elero_ns.class_("Sx1276Driver", spi.SPIDevice)

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
CONF_BUSY_PIN = "busy_pin"
CONF_RST_PIN = "rst_pin"
CONF_RF_SWITCH = "rf_switch"
CONF_PA_POWER = "pa_power"
CONF_TCXO_VOLTAGE = "tcxo_voltage"
CONF_FEM_PA_PIN = "fem_pa_pin"


def _validate_irq_pin(config):
    """Accept either irq_pin or gdo0_pin (backward compat)."""
    if CONF_IRQ_PIN not in config and CONF_GDO0_PIN not in config:
        raise cv.Invalid(f"Either '{CONF_IRQ_PIN}' or '{CONF_GDO0_PIN}' is required")
    return config


def _validate_sx1262_pins(config):
    """SX1262 requires busy_pin and rst_pin."""
    if config.get(CONF_RADIO) == "sx1262":
        if CONF_BUSY_PIN not in config:
            raise cv.Invalid(f"'{CONF_BUSY_PIN}' is required for SX1262 radio")
        if CONF_RST_PIN not in config:
            raise cv.Invalid(f"'{CONF_RST_PIN}' is required for SX1262 radio")
    return config


def _validate_sx1276_pins(config):
    """SX1276 requires rst_pin and has a different PA power range."""
    if config.get(CONF_RADIO) == "sx1276":
        if CONF_RST_PIN not in config:
            raise cv.Invalid(f"'{CONF_RST_PIN}' is required for SX1276 radio")
        pa = config.get(CONF_PA_POWER, 17)
        if pa < -1:
            raise cv.Invalid(
                f"SX1276 RFO supports min -1 dBm (got {pa})."
            )
        if pa > 20:
            raise cv.Invalid(
                f"SX1276 supports max +20 dBm (got {pa}). "
                f"Use pa_power: 20 for maximum output."
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(elero),
            cv.GenerateID(CONF_REGISTRY_ID): cv.declare_id(DeviceRegistry),
            cv.GenerateID(CONF_DRIVER_ID): cv.declare_id(CC1101Driver),
            cv.Optional(CONF_RADIO, default="cc1101"): cv.one_of(
                "cc1101", "sx1262", "sx1276", lower=True
            ),
            cv.Optional(CONF_IRQ_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_GDO0_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_FREQ0, default=0x7A): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ1, default=0x71): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_FREQ2, default=0x21): cv.hex_int_range(min=0x0, max=0xFF),
            cv.Optional(CONF_AUTO_STATS, default=True): cv.boolean,
            # SX1262-specific pins
            cv.Optional(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_RST_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_FEM_PA_PIN): pins.gpio_output_pin_schema,
            # SX1262-specific options
            cv.Optional(CONF_RF_SWITCH, default=False): cv.boolean,
            # Schema max=22 for SX1262; SX1276 validator narrows to max=20
            cv.Optional(CONF_PA_POWER): cv.int_range(min=-3, max=22),
            cv.Optional(CONF_TCXO_VOLTAGE): cv.float_range(min=1.6, max=3.3),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True)),
    _validate_irq_pin,
    _validate_sx1262_pins,
    _validate_sx1276_pins,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    radio = config[CONF_RADIO]

    if radio == "sx1262":
        # Override driver_id type for SX1262
        driver_id = config[CONF_DRIVER_ID]
        driver_id.type = Sx1262Driver
        driver = cg.new_Pvariable(driver_id)
        await spi.register_spi_device(driver, config)
        cg.add(driver.set_freq0(config[CONF_FREQ0]))
        cg.add(driver.set_freq1(config[CONF_FREQ1]))
        cg.add(driver.set_freq2(config[CONF_FREQ2]))

        # SX1262-specific pins
        busy_pin = await cg.gpio_pin_expression(config[CONF_BUSY_PIN])
        cg.add(driver.set_busy_pin(busy_pin))
        rst_pin = await cg.gpio_pin_expression(config[CONF_RST_PIN])
        cg.add(driver.set_rst_pin(rst_pin))

        # FEM PA pin (optional — Heltec V4 uses GPIO46 for external PA enable)
        if CONF_FEM_PA_PIN in config:
            fem_pa_pin = await cg.gpio_pin_expression(config[CONF_FEM_PA_PIN])
            cg.add(driver.set_fem_pa_pin(fem_pa_pin))

        # SX1262-specific options
        cg.add(driver.set_rf_switch(config[CONF_RF_SWITCH]))
        cg.add(driver.set_pa_power(config.get(CONF_PA_POWER, 22)))
        if CONF_TCXO_VOLTAGE in config:
            cg.add(driver.set_tcxo_voltage(config[CONF_TCXO_VOLTAGE]))
    elif radio == "sx1276":
        # Override driver_id type for SX1276
        driver_id = config[CONF_DRIVER_ID]
        driver_id.type = Sx1276Driver
        driver = cg.new_Pvariable(driver_id)
        await spi.register_spi_device(driver, config)
        cg.add(driver.set_freq0(config[CONF_FREQ0]))
        cg.add(driver.set_freq1(config[CONF_FREQ1]))
        cg.add(driver.set_freq2(config[CONF_FREQ2]))

        # SX1276 RST pin
        rst_pin = await cg.gpio_pin_expression(config[CONF_RST_PIN])
        cg.add(driver.set_rst_pin(rst_pin))

        # PA power (SX1276 default: +17 dBm on PA_BOOST)
        cg.add(driver.set_pa_power(config.get(CONF_PA_POWER, 17)))
    else:
        # CC1101 driver (default)
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
        from esphome.components import sensor  # noqa: F401

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
