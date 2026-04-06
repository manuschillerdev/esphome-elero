"""Tests for cover platform config validation.

Tests the _validate_duration_consistency validator defined in
components/elero/cover/__init__.py.
"""

import pytest
from esphome.config_validation import Invalid

from conftest import make_time_period


# Import the validator directly from the cover __init__.py
from elero.cover import _validate_duration_consistency, CONF_OPEN_DURATION, CONF_CLOSE_DURATION


class TestDurationConsistency:
    """Both open_duration and close_duration must be set together or both zero."""

    def test_both_zero_passes(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(0),
            CONF_CLOSE_DURATION: make_time_period(0),
        }
        result = _validate_duration_consistency(config)
        assert result is config

    def test_both_nonzero_passes(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(25000),
            CONF_CLOSE_DURATION: make_time_period(22000),
        }
        result = _validate_duration_consistency(config)
        assert result is config

    def test_open_only_raises(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(25000),
            CONF_CLOSE_DURATION: make_time_period(0),
        }
        with pytest.raises(Invalid, match="both open_duration and close_duration"):
            _validate_duration_consistency(config)

    def test_close_only_raises(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(0),
            CONF_CLOSE_DURATION: make_time_period(22000),
        }
        with pytest.raises(Invalid, match="both open_duration and close_duration"):
            _validate_duration_consistency(config)

    def test_equal_durations_passes(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(15000),
            CONF_CLOSE_DURATION: make_time_period(15000),
        }
        result = _validate_duration_consistency(config)
        assert result is config

    def test_small_durations_passes(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(100),
            CONF_CLOSE_DURATION: make_time_period(200),
        }
        result = _validate_duration_consistency(config)
        assert result is config

    def test_error_message_contains_values(self):
        config = {
            CONF_OPEN_DURATION: make_time_period(5000),
            CONF_CLOSE_DURATION: make_time_period(0),
        }
        with pytest.raises(Invalid, match="5000ms"):
            _validate_duration_consistency(config)
