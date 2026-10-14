#!/usr/bin/env python3
"""Migrate legacy `cover: - platform: elero` / `light: - platform: elero`
YAML blocks to a `ConfigSnapshot` JSON file consumable by the web UI's
"Restore from backup" action.

Run with `uv run scripts/migrate_yaml_to_json.py example.yaml -o backup.json`.
The output is the same envelope the server emits via `export_config`,
so the round-trip (YAML → JSON → import → NVS → export → JSON) is
byte-stable for the device-config fields.

Notes
-----
- This script intentionally has zero ESPHome dependencies — it only needs
  PyYAML so that the migration works in a tiny venv unrelated to the
  firmware build.
- ESPHome time periods (e.g. "25s", "500ms", "1min") and hex literals
  (`0xa831e5`) are parsed directly; we don't import esphome.config_validation
  to avoid pulling the entire framework.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path
from typing import Any

import yaml

SNAPSHOT_VERSION = 1
EXPORTER_DEVICE = "yaml-migration"
EXPORTER_VERSION = "1"

# Defaults must mirror the C++ packet::defaults namespace and the cover/light
# platform schemas. If these drift out of sync, importing a migrated snapshot
# would change behavior — we want byte-identical output to the legacy YAML path.
DEFAULTS = {
    "hop": 0x0A,
    "payload_1": 0x00,
    "payload_2": 0x04,
    "msg_type": 0x6A,  # ESPHome name was "type" — we rename here for the snapshot
    "type2": 0x00,
}

# Map ESPHome cover device_class string → HaCoverClass enum. Mirrors the table
# in components/elero/cover/__init__.py.
DEVICE_CLASS_MAP = {
    "shutter": 0,
    "blind": 1,
    "awning": 2,
    "curtain": 3,
    "shade": 4,
    "garage": 5,
}

_TIME_RE = re.compile(r"^\s*(\d+)\s*(ms|s|min|h)?\s*$", re.IGNORECASE)


def parse_duration_ms(value: Any) -> int:
    """Parse an ESPHome time period (string or int) into milliseconds.

    Accepts: int (ms), "500ms", "25s", "5min", "1h". Returns 0 for None / "0".
    """
    if value is None:
        return 0
    if isinstance(value, int):
        return value
    if not isinstance(value, str):
        raise ValueError(f"Unsupported duration value: {value!r}")
    m = _TIME_RE.match(value)
    if not m:
        raise ValueError(f"Cannot parse duration {value!r}")
    n = int(m.group(1))
    unit = (m.group(2) or "ms").lower()
    return {
        "ms": n,
        "s": n * 1000,
        "min": n * 60_000,
        "h": n * 3_600_000,
    }[unit]


def to_hex_address(value: Any) -> str:
    """Normalize an address to the `0xNNNNNN` 6-hex-digit form used by the snapshot."""
    if isinstance(value, str):
        n = int(value, 0)
    else:
        n = int(value)
    return f"0x{n:06x}"


def to_hex_byte(value: Any) -> str:
    if isinstance(value, str):
        n = int(value, 0)
    else:
        n = int(value)
    return f"0x{n & 0xFF:02x}"


def _byte_or_default(entry: dict[str, Any], yaml_key: str, default: int) -> str:
    """Read an optional byte field, falling back to the C++/schema default."""
    return to_hex_byte(entry.get(yaml_key, default))


def convert_cover(entry: dict[str, Any]) -> dict[str, Any]:
    if "dst_address" not in entry or "src_address" not in entry or "channel" not in entry:
        raise ValueError(f"Cover entry missing required address/channel: {entry!r}")
    out: dict[str, Any] = {
        "device_type": "cover",
        "dst_address": to_hex_address(entry["dst_address"]),
        "src_address": to_hex_address(entry["src_address"]),
        "channel": int(entry["channel"]),
        "name": entry.get("name", ""),
        "enabled": True,
        "open_duration_ms": parse_duration_ms(entry.get("open_duration", 0)),
        "close_duration_ms": parse_duration_ms(entry.get("close_duration", 0)),
        "supports_tilt": bool(entry.get("supports_tilt", False)),
        "ha_device_class": DEVICE_CLASS_MAP.get(str(entry.get("device_class", "shutter")).lower(), 0),
        "hop": _byte_or_default(entry, "hop", DEFAULTS["hop"]),
        "payload_1": _byte_or_default(entry, "payload_1", DEFAULTS["payload_1"]),
        "payload_2": _byte_or_default(entry, "payload_2", DEFAULTS["payload_2"]),
        "msg_type": _byte_or_default(entry, "type", DEFAULTS["msg_type"]),
        "type2": _byte_or_default(entry, "type2", DEFAULTS["type2"]),
    }
    for key in ("command_up", "command_down", "command_stop", "command_check", "command_tilt"):
        if key in entry:
            raw = entry[key]
            value = int(raw, 0) if isinstance(raw, str) else int(raw)
            if not 0 <= value <= 255:
                raise ValueError(f"{key} must be a byte (0..255), got {raw!r}")
            out[key] = to_hex_byte(value)
    return out


def convert_light(entry: dict[str, Any]) -> dict[str, Any]:
    if "dst_address" not in entry or "src_address" not in entry or "channel" not in entry:
        raise ValueError(f"Light entry missing required address/channel: {entry!r}")
    out: dict[str, Any] = {
        "device_type": "light",
        "dst_address": to_hex_address(entry["dst_address"]),
        "src_address": to_hex_address(entry["src_address"]),
        "channel": int(entry["channel"]),
        "name": entry.get("name", ""),
        "enabled": True,
        "dim_duration_ms": parse_duration_ms(entry.get("dim_duration", 0)),
        "hop": _byte_or_default(entry, "hop", DEFAULTS["hop"]),
        "payload_1": _byte_or_default(entry, "payload_1", DEFAULTS["payload_1"]),
        "payload_2": _byte_or_default(entry, "payload_2", DEFAULTS["payload_2"]),
        "msg_type": _byte_or_default(entry, "type", DEFAULTS["msg_type"]),
        "type2": _byte_or_default(entry, "type2", DEFAULTS["type2"]),
    }
    return out


def _entries_for_platform(top: dict[str, Any], key: str) -> list[dict[str, Any]]:
    """Extract `platform: elero` entries from a top-level YAML key.

    ESPHome accepts either a list or a single mapping at top-level platform
    keys. Non-elero entries are skipped silently.
    """
    raw = top.get(key)
    if raw is None:
        return []
    if isinstance(raw, dict):
        raw = [raw]
    if not isinstance(raw, list):
        raise ValueError(f"Top-level `{key}:` must be a list or mapping")
    return [entry for entry in raw if isinstance(entry, dict) and entry.get("platform") == "elero"]


def build_snapshot(yaml_doc: dict[str, Any]) -> dict[str, Any]:
    devices: list[dict[str, Any]] = []
    for entry in _entries_for_platform(yaml_doc, "cover"):
        devices.append(convert_cover(entry))
    for entry in _entries_for_platform(yaml_doc, "light"):
        devices.append(convert_light(entry))

    hub: dict[str, Any] = {}
    # Carry over the elero_mqtt device_name as a hub name override so the
    # restored gateway shows up under the same HA device name. If the legacy
    # YAML doesn't specify one, leave the hub block empty (server keeps default).
    mqtt = yaml_doc.get("elero_mqtt")
    if isinstance(mqtt, dict) and mqtt.get("device_name"):
        hub["name_override"] = str(mqtt["device_name"])

    snapshot = {
        "snapshot_version": SNAPSHOT_VERSION,
        "exported_at": int(time.time() * 1000),
        "exporter": {"device": EXPORTER_DEVICE, "version": EXPORTER_VERSION},
        "hub": hub,
        "devices": devices,
    }
    return snapshot


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="Legacy ESPHome YAML to migrate")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Output JSON path (default: <input>.backup.json)",
    )
    args = parser.parse_args(argv)

    if not args.input.exists():
        print(f"error: {args.input} does not exist", file=sys.stderr)
        return 2

    out_path = args.output or args.input.with_suffix(args.input.suffix + ".backup.json")

    with args.input.open("r", encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    if not isinstance(doc, dict):
        print(f"error: {args.input} did not parse as a YAML mapping", file=sys.stderr)
        return 2

    snapshot = build_snapshot(doc)

    out_path.write_text(json.dumps(snapshot, indent=2) + "\n", encoding="utf-8")
    print(
        f"Wrote {out_path} ({len(snapshot['devices'])} device{'s' if len(snapshot['devices']) != 1 else ''})",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
