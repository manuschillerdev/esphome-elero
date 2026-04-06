"""Tests for hub config validation.

Tests _validate_irq_pin and _validate_sx1262_pins validators defined in
components/elero/__init__.py.
"""

import pytest
from esphome.config_validation import Invalid

from elero import (
    _validate_irq_pin,
    _validate_sx1262_pins,
    CONF_GDO0_PIN,
    CONF_IRQ_PIN,
    CONF_RADIO,
    CONF_BUSY_PIN,
    CONF_RST_PIN,
)


class TestIrqPinValidation:
    """Either irq_pin or gdo0_pin must be provided."""

    def test_irq_pin_only_passes(self):
        config = {CONF_IRQ_PIN: {"number": 26}}
        result = _validate_irq_pin(config)
        assert result is config

    def test_gdo0_pin_only_passes(self):
        config = {CONF_GDO0_PIN: {"number": 26}}
        result = _validate_irq_pin(config)
        assert result is config

    def test_both_pins_passes(self):
        config = {CONF_IRQ_PIN: {"number": 26}, CONF_GDO0_PIN: {"number": 27}}
        result = _validate_irq_pin(config)
        assert result is config

    def test_neither_pin_raises(self):
        config = {}
        with pytest.raises(Invalid, match="irq_pin.*gdo0_pin|gdo0_pin.*irq_pin"):
            _validate_irq_pin(config)


class TestSx1262PinValidation:
    """SX1262 radio requires busy_pin and rst_pin."""

    def test_cc1101_no_extra_pins_passes(self):
        config = {CONF_RADIO: "cc1101"}
        result = _validate_sx1262_pins(config)
        assert result is config

    def test_sx1262_with_both_pins_passes(self):
        config = {
            CONF_RADIO: "sx1262",
            CONF_BUSY_PIN: {"number": 36},
            CONF_RST_PIN: {"number": 12},
        }
        result = _validate_sx1262_pins(config)
        assert result is config

    def test_sx1262_missing_busy_raises(self):
        config = {
            CONF_RADIO: "sx1262",
            CONF_RST_PIN: {"number": 12},
        }
        with pytest.raises(Invalid, match="busy_pin"):
            _validate_sx1262_pins(config)

    def test_sx1262_missing_rst_raises(self):
        config = {
            CONF_RADIO: "sx1262",
            CONF_BUSY_PIN: {"number": 36},
        }
        with pytest.raises(Invalid, match="rst_pin"):
            _validate_sx1262_pins(config)

    def test_sx1262_missing_both_raises(self):
        config = {CONF_RADIO: "sx1262"}
        with pytest.raises(Invalid, match="busy_pin"):
            _validate_sx1262_pins(config)

    def test_no_radio_key_passes(self):
        """Config without radio key shouldn't fail (defaults to cc1101)."""
        config = {}
        result = _validate_sx1262_pins(config)
        assert result is config
