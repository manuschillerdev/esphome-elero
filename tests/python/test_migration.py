"""Tests for `scripts/migrate_yaml_to_json.py` — legacy YAML → snapshot JSON.

The migration script is the only path users have to recover their existing
YAML-defined devices after the YAML platforms are removed (RFC-002 Phase 3),
so we exhaustively pin its behavior:

- Field defaults (hop, payloads, msg_type, type2) match the C++/schema defaults.
- Time-period parsing handles `25s`, `500ms`, `5min`, `1h`, raw ints.
- The output envelope round-trips through the same shape the server emits
  via `export_config` / consumes via `import_config`.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

# Make the script importable as a module.
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import migrate_yaml_to_json as mig  # noqa: E402

FIXTURES = Path(__file__).parent / "fixtures"


# ─── Duration parsing ────────────────────────────────────────────────────────


class TestParseDurationMs:
    def test_none_returns_zero(self):
        assert mig.parse_duration_ms(None) == 0

    def test_int_passthrough(self):
        assert mig.parse_duration_ms(500) == 500

    @pytest.mark.parametrize(
        ("raw", "expected"),
        [
            ("0s", 0),
            ("500ms", 500),
            ("25s", 25_000),
            ("5min", 5 * 60_000),
            ("2h", 2 * 3_600_000),
            ("100", 100),  # bare number → ms
        ],
    )
    def test_string_units(self, raw, expected):
        assert mig.parse_duration_ms(raw) == expected

    def test_invalid_raises(self):
        with pytest.raises(ValueError):
            mig.parse_duration_ms("never")


# ─── Address / byte normalization ────────────────────────────────────────────


class TestAddressNormalization:
    def test_hex_string(self):
        assert mig.to_hex_address("0xa831e5") == "0xa831e5"

    def test_int(self):
        assert mig.to_hex_address(0xA831E5) == "0xa831e5"

    def test_pads_short_int(self):
        assert mig.to_hex_address(0x10) == "0x000010"

    def test_byte_truncates(self):
        assert mig.to_hex_byte(0x12A) == "0x2a"


# ─── Cover conversion ────────────────────────────────────────────────────────


def _minimal_cover() -> dict:
    return {
        "platform": "elero",
        "name": "Test",
        "dst_address": 0xA831E5,
        "src_address": 0xF0D008,
        "channel": 4,
    }


class TestConvertCover:
    def test_minimal_uses_defaults(self):
        out = mig.convert_cover(_minimal_cover())
        assert out["device_type"] == "cover"
        assert out["dst_address"] == "0xa831e5"
        assert out["src_address"] == "0xf0d008"
        assert out["channel"] == 4
        assert out["enabled"] is True
        # Defaults must mirror C++ packet::defaults
        assert out["hop"] == "0x0a"
        assert out["payload_1"] == "0x00"
        assert out["payload_2"] == "0x04"
        assert out["msg_type"] == "0x6a"
        assert out["type2"] == "0x00"
        assert out["open_duration_ms"] == 0
        assert out["close_duration_ms"] == 0
        assert out["supports_tilt"] is False
        assert out["ha_device_class"] == 0  # shutter

    def test_durations_parsed(self):
        entry = _minimal_cover() | {"open_duration": "25s", "close_duration": "22s"}
        out = mig.convert_cover(entry)
        assert out["open_duration_ms"] == 25_000
        assert out["close_duration_ms"] == 22_000

    def test_supports_tilt_and_class(self):
        entry = _minimal_cover() | {"supports_tilt": True, "device_class": "blind"}
        out = mig.convert_cover(entry)
        assert out["supports_tilt"] is True
        assert out["ha_device_class"] == 1

    def test_unknown_class_falls_back_to_shutter(self):
        entry = _minimal_cover() | {"device_class": "weird"}
        out = mig.convert_cover(entry)
        assert out["ha_device_class"] == 0

    def test_payload_overrides_propagate(self):
        entry = _minimal_cover() | {"payload_1": 0x42, "type": 0x69, "hop": 0x0F}
        out = mig.convert_cover(entry)
        assert out["payload_1"] == "0x42"
        assert out["msg_type"] == "0x69"
        assert out["hop"] == "0x0f"

    def test_missing_required_raises(self):
        for missing in ("dst_address", "src_address", "channel"):
            entry = _minimal_cover()
            del entry[missing]
            with pytest.raises(ValueError):
                mig.convert_cover(entry)


# ─── Light conversion ────────────────────────────────────────────────────────


def _minimal_light() -> dict:
    return {
        "platform": "elero",
        "name": "Test Light",
        "dst_address": 0xC41A2B,
        "src_address": 0xD51E03,
        "channel": 3,
    }


class TestConvertLight:
    def test_minimal(self):
        out = mig.convert_light(_minimal_light())
        assert out["device_type"] == "light"
        assert out["dim_duration_ms"] == 0
        assert "supports_tilt" not in out  # Light-only schema has no tilt

    def test_dim_duration(self):
        entry = _minimal_light() | {"dim_duration": "5s"}
        out = mig.convert_light(entry)
        assert out["dim_duration_ms"] == 5_000


# ─── End-to-end snapshot build ───────────────────────────────────────────────


class TestBuildSnapshot:
    def test_fixture_round_trip(self, tmp_path):
        with (FIXTURES / "legacy_mixed.yaml").open("r", encoding="utf-8") as f:
            import yaml

            doc = yaml.safe_load(f)
        snap = mig.build_snapshot(doc)

        # Envelope shape matches what the server emits
        assert snap["snapshot_version"] == mig.SNAPSHOT_VERSION
        assert isinstance(snap["exported_at"], int) and snap["exported_at"] > 0
        assert snap["exporter"]["device"] == mig.EXPORTER_DEVICE

        # 2 covers + 1 light, the `platform: template` entry must be skipped.
        types = sorted(d["device_type"] for d in snap["devices"])
        assert types == ["cover", "cover", "light"]

        # Hub override carried over from elero_mqtt.device_name
        assert snap["hub"]["name_override"] == "Living Room Hub"

        # Spot-check one cover end-to-end: payload defaults + parsed open_duration
        living = next(d for d in snap["devices"] if d["device_type"] == "cover" and d["dst_address"] == "0xa831e5")
        assert living["open_duration_ms"] == 25_000
        assert living["close_duration_ms"] == 22_000
        assert living["supports_tilt"] is True
        assert living["ha_device_class"] == 1  # blind
        assert living["msg_type"] == "0x6a"

        # Spot-check the light keeps its custom payload_2 override
        light = next(d for d in snap["devices"] if d["device_type"] == "light")
        assert light["payload_2"] == "0x05"
        assert light["dim_duration_ms"] == 5_000

    def test_snapshot_is_json_serializable(self):
        # Whatever else changes, the output must be valid JSON.
        snap = mig.build_snapshot({})
        assert json.loads(json.dumps(snap)) == snap

    def test_no_elero_devices_yields_empty_devices_array(self):
        snap = mig.build_snapshot({"esphome": {"name": "foo"}})
        assert snap["devices"] == []
        assert snap["hub"] == {}


# ─── CLI smoke ───────────────────────────────────────────────────────────────


class TestCli:
    def test_cli_writes_output(self, tmp_path, capsys):
        out = tmp_path / "snap.json"
        rc = mig.main([str(FIXTURES / "legacy_mixed.yaml"), "-o", str(out)])
        assert rc == 0
        assert out.exists()
        data = json.loads(out.read_text())
        assert data["snapshot_version"] == 1
        assert len(data["devices"]) == 3

    def test_cli_missing_input_fails(self, tmp_path, capsys):
        rc = mig.main([str(tmp_path / "nope.yaml"), "-o", str(tmp_path / "out.json")])
        assert rc == 2
