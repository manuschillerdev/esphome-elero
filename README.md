# esphome-elero

ESPHome component for controlling Elero wireless blinds and lights via an ESP32 with a CC1101, SX1262, or SX1276 868 MHz RF transceiver. Bidirectional -- sends commands and receives status feedback.

[![ESPHome](https://img.shields.io/badge/ESPHome-Component-blue)](https://esphome.io/)
[![License](https://img.shields.io/badge/License-MIT-blue)](LICENSE)

## Quick Start

### 1. Choose your hardware

Pick a device config for your board:

- [ESP32 + CC1101](docs/devices/esp32-cc1101.md) -- generic ESP32 with external CC1101 module
- [Heltec WiFi LoRa 32 V4](docs/devices/heltec-lora-v4.md) -- onboard SX1262 (experimental)
- [LILYGO LoRa32 V2.1](docs/devices/lilygo-lora32-sx1276.md) -- onboard SX1276 (experimental)
- [LilyGO T-Embed](docs/devices/lilygo-t-embed.md) -- onboard CC1101

### 2. Choose your output adapter

Devices always live in NVS and are managed at runtime through the web UI. The adapter only decides how Home Assistant sees them:

**`elero_nvs:`** -- ESPHome native API. Recommended for HA-only setups.

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero

# ... board + SPI + elero config from your device page ...

api:
elero_nvs:
elero_web:
```

**`elero_mqtt:`** -- MQTT HA discovery. Use when you already run an MQTT broker, or when you want device CRUD to apply without rebooting.

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero

# ... board + SPI + elero config from your device page ...

mqtt:
  broker: !secret mqtt_broker

elero_mqtt:
  topic_prefix: elero
  discovery_prefix: homeassistant
  device_name: "Elero Gateway"

elero_web:
```

### 3. Flash, discover, and add devices

1. Flash your config (`uv run esphome run your-config.yaml`).
2. Open `http://<device-ip>/elero`.
3. Press buttons on your physical Elero remote -- RF packets appear in real time and remotes/blinds are auto-discovered.
4. Save discovered devices in the web UI.
5. (Optional) **Backup**: Hub → Backup & Restore → Download. Keep this JSON safe -- it's how you recover devices after flashing a replacement chip.

### Migrating from older versions (YAML-defined devices)

If you're upgrading from a version where devices were defined under `cover: - platform: elero` / `light: - platform: elero`, see [docs/MIGRATION-yaml-to-nvs.md](docs/MIGRATION-yaml-to-nvs.md). The TL;DR: run `uv run scripts/migrate_yaml_to_json.py old.yaml -o backup.json`, remove the `cover:` / `light:` blocks from YAML, flash, then upload the backup via the web UI.

Group transmissions support up to **31 destination channels per source address**
on CC1101, SX1262, and SX1276. See [radio framing and limits](docs/RADIO_FRAMING.md#supported-group-size).

## Documentation

- [Configuration Reference](docs/CONFIGURATION.md) -- full parameter tables for all modes
- [Installation Guide](docs/INSTALLATION.md) -- step-by-step hardware setup
- [Backup &amp; Restore](docs/BACKUP-RESTORE.md) -- exporting/importing your NVS device list
- [Migration from YAML devices](docs/MIGRATION-yaml-to-nvs.md) -- upgrading from pre-0.11.0
- [Device Configs](docs/devices/) -- board-specific wiring and config
- [example.yaml](example.yaml) -- minimal working config

## Credits

Based on protocol research by [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT), [stanleypa/eleropy](https://github.com/stanleypa/eleropy) (GPLv3), and [andyboeh/esphome-elero](https://github.com/andyboeh/esphome-elero).
