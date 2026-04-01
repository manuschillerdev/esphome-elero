# esphome-elero

ESPHome component for controlling Elero wireless blinds and lights via an ESP32 with a CC1101 or SX1262 868 MHz RF transceiver. Bidirectional -- sends commands and receives status feedback.

[![ESPHome](https://img.shields.io/badge/ESPHome-Component-blue)](https://esphome.io/)
[![License](https://img.shields.io/badge/License-MIT-blue)](LICENSE)

## Quick Start

### 1. Choose your hardware

Pick a device config for your board:

- [ESP32 + CC1101](docs/devices/esp32-cc1101.md) -- generic ESP32 with external CC1101 module
- [Heltec WiFi LoRa 32 V4](docs/devices/heltec-lora-v4.md) -- onboard SX1262 (experimental)
- [LilyGO T-Embed](docs/devices/lilygo-t-embed.md) -- onboard CC1101

### 2. Choose your mode

**MQTT mode** (recommended) -- manage devices at runtime via the web UI, no reflashing needed:

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero

# ... board + SPI + elero config from your device page ...

elero_web:

mqtt:
  broker: !secret mqtt_broker

elero_mqtt:
  topic_prefix: elero
  discovery_prefix: homeassistant
  device_name: "Elero Gateway"
```

**YAML mode** -- define devices in YAML at compile time:

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero

# ... board + SPI + elero config from your device page ...

elero_web:

cover:
  - platform: elero
    name: "Living Room"
    dst_address: 0xa831e5
    channel: 4
    src_address: 0xf0d008

light:
  - platform: elero
    name: "Patio Light"
    dst_address: 0xc41a2b
    channel: 6
    src_address: 0xf0d008
```

### 3. Flash and discover

1. Flash your config (`esphome run your-config.yaml`)
2. Open `http://<device-ip>/elero`
3. Press buttons on your physical Elero remote -- RF packets appear in real time
4. **MQTT mode:** save discovered devices in the web UI, they appear in Home Assistant automatically
5. **YAML mode:** copy the discovered addresses into your YAML config and reflash

## Documentation

- [Configuration Reference](docs/CONFIGURATION.md) -- full parameter tables for all modes
- [Installation Guide](docs/INSTALLATION.md) -- step-by-step hardware setup
- [Device Configs](docs/devices/) -- board-specific wiring and config
- [example.yaml](example.yaml) -- minimal working config

## Credits

Based on protocol research by [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT), [stanleypa/eleropy](https://github.com/stanleypa/eleropy) (GPLv3), and [andyboeh/esphome-elero](https://github.com/andyboeh/esphome-elero).
