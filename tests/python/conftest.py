"""Shared fixtures for ESPHome config validation tests."""

import sys
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest
from esphome.config_validation import positive_time_period_milliseconds
from esphome.core import TimePeriodMilliseconds

# Add components directory to sys.path so we can import our __init__.py modules
COMPONENTS_DIR = Path(__file__).resolve().parent.parent.parent / "components"
sys.path.insert(0, str(COMPONENTS_DIR))


def make_time_period(ms: int) -> TimePeriodMilliseconds:
    """Create a TimePeriodMilliseconds from integer milliseconds."""
    return positive_time_period_milliseconds(f"{ms}ms")


@pytest.fixture()
def mock_esphome_core():
    """Mock ESPHome CORE so config validation doesn't try to access hardware."""
    mock_core = MagicMock()
    mock_core.is_esp32 = True
    mock_core.is_esp8266 = False
    mock_core.is_rp2040 = False
    with patch("esphome.core.CORE", mock_core):
        yield mock_core
