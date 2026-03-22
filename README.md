# esphome-elero

ESPHome component for controlling Elero wireless blinds and lights via an ESP32 with a CC1101 868 MHz RF transceiver. Bidirectional — sends commands and receives status feedback.

[![ESPHome](https://img.shields.io/badge/ESPHome-Component-blue)](https://esphome.io/)
[![License](https://img.shields.io/badge/License-MIT-blue)](LICENSE)

## Features

- Open / close / stop / tilt covers, on / off / dim lights
- Bidirectional RF: receive blind status (position, moving, blocked, overheated)
- Time-based position tracking (open/close duration calibration)
- Web UI at `http://<device-ip>/elero` for RF discovery and device management
- Three operating modes: Native (YAML), MQTT (NVS + HA discovery), Native+NVS
- RSSI signal strength and diagnostic sensors per device
- Modular radio driver architecture (CC1101 today, SX1262 planned)

## Quick Start

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero

esp32:
  board: esp32dev

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

# Enable the web UI for device discovery
elero_web:

cover:
  - platform: elero
    name: "Living Room"
    dst_address: 0xa831e5     # Blind address (discovered via web UI)
    channel: 4
    src_address: 0xf0d008     # Remote address (discovered via web UI)
    open_duration: 25s        # Optional: enables position tracking
    close_duration: 22s
    supports_tilt: true       # Optional: enables tilt control

light:
  - platform: elero
    name: "Patio Light"
    dst_address: 0xc41a2b
    channel: 6
    src_address: 0xf0d008
    dim_duration: 5s          # Optional: 0s = on/off only
```

## Discovering Blind Addresses

1. Add `elero_web:` to your config and flash
2. Open `http://<device-ip>/elero`
3. Press buttons on your physical Elero remote
4. RF packets appear in the web UI — addresses are shown automatically
5. Add the discovered addresses to your YAML config

## Operating Modes

| Mode | Devices defined in | Home Assistant API | Component |
|---|---|---|---|
| **Native** | YAML (compile-time) | ESPHome native API | `elero:` only |
| **MQTT** | NVS (runtime via web UI) | MQTT HA discovery | `elero:` + `elero_mqtt:` |
| **Native+NVS** | NVS (runtime, reboot to apply) | ESPHome native API | `elero:` + `elero_nvs:` |

MQTT mode enables runtime device management without reflashing — add, edit, and remove devices via the web UI.

## Frequency

Most European Elero motors use **868.35 MHz** (default). Some variants use 868.95 MHz. If discovery finds nothing, try the alternate frequency:

```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0xc0    # 868.95 MHz (default: 0x7a = 868.35 MHz)
```

## Hardware

**Supported:** ESP32 (all variants including ESP32-S3) with CC1101 868 MHz module.

Wiring (5 connections):

| CC1101 | ESP32 | Function |
|---|---|---|
| VCC | 3V3 | Power (3.3V only) |
| GND | GND | Ground |
| SCK | SPI CLK | SPI clock |
| MOSI | SPI MOSI | SPI data out |
| MISO | SPI MISO | SPI data in |
| CSN | Any GPIO | SPI chip select |
| GDO0 | Any GPIO | Interrupt pin |

GDO2 is not used. SPI pins are configurable — any valid GPIO works.

## Documentation

- [docs/CONFIGURATION.md](docs/CONFIGURATION.md) — Full parameter reference
- [docs/INSTALLATION.md](docs/INSTALLATION.md) — Step-by-step hardware setup
- [example.yaml](example.yaml) — Minimal working config

## Development

Requires [uv](https://docs.astral.sh/uv/) and [mise](https://mise.jdx.dev/).

```bash
mise run setup          # Install dependencies
mise run compile        # Compile firmware
mise run test           # Run unit tests (335 tests)
mise run deploy:ota     # Build + flash via OTA
```

Web UI development:

```bash
cd components/elero_web/frontend/app
pnpm dev                           # Dev server with mock data
DEVICE_IP=10.0.0.4 pnpm dev       # Dev server against real device
pnpm build                         # Production build (generates elero_web_ui.h)
```

## Credits

Based on protocol research by:

- [QuadCorei8085/elero_protocol](https://github.com/QuadCorei8085/elero_protocol) (MIT) — Encryption/decryption
- [stanleypa/eleropy](https://github.com/stanleypa/eleropy) (GPLv3) — Remote handling
- [andyboeh/esphome-elero](https://github.com/andyboeh/esphome-elero) — Original ESPHome component (this repo is a near-complete rebuild)
