# Installation Guide: Elero ESPHome Component

This guide walks you through the complete setup of the Elero component step by step, from assembling the hardware to a fully working Home Assistant integration.

---

## Table of Contents

1. [Required Parts](#1-required-parts)
2. [Hardware Assembly](#2-hardware-assembly)
3. [Installing ESPHome](#3-installing-esphome)
4. [Initial Setup: Testing the Frequency](#4-initial-setup-testing-the-frequency)
5. [Finding Blind Addresses](#5-finding-blind-addresses)
6. [Final Configuration](#6-final-configuration)
7. [Adding to Home Assistant](#7-adding-to-home-assistant)
8. [Adding Multiple Blinds](#8-adding-multiple-blinds)

---

## 1. Required Parts

### Shopping List

| Part | Description | Approximate Price |
|---|---|---|
| ESP32 Board | e.g. D1 Mini ESP32, ESP32-DevKit | ~5-10 EUR |
| CC1101 Module | 868 MHz recommended for Europe | ~3-5 EUR |
| Dupont Wires | 7x female-to-female | ~2 EUR |
| USB Cable | Micro-USB or USB-C (depending on board) | usually on hand |
| Elero Remote Control | e.g. TempoTel 2 (already owned) | - |

> **Tip:** 868 MHz CC1101 modules have significantly better range than 433 MHz. Use 868 MHz if possible.

### Tools

- Computer with a USB port
- Python 3.9+ installed
- Internet access

---

## 2. Hardware Assembly

### Step 2.1: Connect CC1101 to ESP32

Connect the pins as follows using Dupont wires:

```
CC1101          ESP32 (Standard-Pinout)
──────          ──────────────────────
VCC  ──────>    3V3  (3.3 Volt!)
GND  ──────>    GND
SCK  ──────>    GPIO18
MOSI ──────>    GPIO23
MISO ──────>    GPIO19
CSN  ──────>    GPIO5
GDO0 ──────>    GPIO26
```

> **WARNING:** The CC1101 operates at **3.3V**. Never connect it to 5V!

### Step 2.2: Verify Wiring

Double-check all connections:
- No loose wires
- No short circuits between adjacent pins
- VCC connected to 3.3V (not 5V!)

### Step 2.3: Connect ESP32 to PC

Connect the ESP32 to your computer via USB. The driver should install automatically.

---

## 3. Installing ESPHome

### Option A: ESPHome Dashboard (Home Assistant Add-on)

1. In Home Assistant: **Settings > Add-ons > Add-on Store**
2. Search for "ESPHome"
3. Install and start
4. Open the ESPHome Dashboard

### Option B: ESPHome CLI (Command Line)

```bash
# Python and pip must be installed
pip install esphome

# Verify it works
esphome version
```

---

## 4. Initial Setup: Testing the Frequency

Before configuring the blind controls, you need to make sure the CC1101 communication is working.

### Step 4.1: Create the Base Configuration

Create the file `elero-blinds.yaml`:

```yaml
esphome:
  name: elero-blinds
  friendly_name: "Elero Rollladen"

esp32:
  board: esp32dev

wifi:
  ssid: "YOUR_WIFI_NAME"
  password: "YOUR_WIFI_PASSWORD"

  ap:
    ssid: "Elero-Fallback"
    password: "fallback123"

logger:
  level: DEBUG

api:
  encryption:
    key: "GenerateThisKeyViaESPHomeDashboard"

ota:
  - platform: esphome
    password: "YourOTAPassword"

# External Elero component
external_components:
  - source: github://pfriedrich84/esphome-elero

# SPI bus
spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

# Elero Hub - default frequency
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

# Web UI for device discovery
web_server_base:
  port: 80

elero_web:
```

### Step 4.2: Flash for the First Time

**Via CLI:**
```bash
# Initial flash over USB
esphome run elero-blinds.yaml
```

**Via Dashboard:**
1. Create a new configuration
2. Paste the YAML
3. Click "Install" > "Plug into this computer"

### Step 4.3: Test the Frequency

1. Open the ESPHome log:
   ```bash
   esphome logs elero-blinds.yaml
   ```
2. Press a button on your Elero remote control
3. **If packets appear in the log** (lines containing `rcv'd: len=...`): The frequency is correct!
4. **If NOTHING happens**: The frequency needs to be adjusted.

### Step 4.4: Adjust the Frequency (if needed)

Change the frequency registers in your configuration:

```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0xc0    # Common alternative instead of 0x7a
  freq1: 0x71
  freq2: 0x21
```

Flash again and test. Common combinations:

| Configuration | freq0 | freq1 | freq2 |
|---|---|---|---|
| Standard 868 MHz | `0x7a` | `0x71` | `0x21` |
| Alternative 868 MHz | `0xc0` | `0x71` | `0x21` |

> **Tip:** Try `0xc0` first -- it is the most common alternative.

---

## 5. Finding Blind Addresses

Open the web UI at `http://<device-ip>/elero`:

1. Operate your physical Elero remote control (Up/Down/Stop)
2. The web UI shows all received RF packets in real time
3. Addresses not already in the configuration are marked as "Discovered"
4. Copy the displayed values into your configuration

The key fields from the RF packets:

| Field | YAML Parameter | Description |
|-------|----------------|-------------|
| `src` | `remote_address` | Address of the remote control |
| `dst` | `blind_address` | Address of the blind |
| `ch` | `channel` | RF channel |

---

## 6. Final Configuration

Insert the discovered values into your configuration:

```yaml
esphome:
  name: elero-blinds
  friendly_name: "Elero Rollladen"

esp32:
  board: esp32dev

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

  ap:
    ssid: "Elero-Fallback"
    password: !secret ap_password

logger:
  level: INFO    # Use DEBUG only when troubleshooting

api:
  encryption:
    key: !secret api_key

ota:
  - platform: esphome
    password: !secret ota_password

external_components:
  - source: github://pfriedrich84/esphome-elero

spi:
  clk_pin: GPIO18
  mosi_pin: GPIO23
  miso_pin: GPIO19

elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26
  freq0: 0xc0     # Adjust if needed
  freq1: 0x71
  freq2: 0x21

# ── Blinds ──
cover:
  - platform: elero
    name: "Schlafzimmer"
    blind_address: 0xa831e5
    channel: 4
    remote_address: 0xf0d008
    open_duration: 25s        # For position tracking (optional)
    close_duration: 22s       # For position tracking (optional)
    poll_interval: 5min

# Diagnostic sensors (RSSI, status, problem, etc.) are created automatically
# by auto_sensors: true (the default) in each cover/light block.

# ── Web UI ──
web_server_base:
  port: 80

elero_web:
```

Flash the final configuration:

```bash
esphome run elero-blinds.yaml
```

---

## 7. Adding to Home Assistant

### Automatic Discovery

If `api:` is configured, the device will be discovered automatically in Home Assistant:

1. **Settings > Devices & Services**
2. The ESPHome device should appear under "Discovered"
3. Click "Configure"

### Manual Setup

If the device is not found automatically:

1. **Settings > Devices & Services > Add Integration**
2. Search for "ESPHome"
3. Enter the IP address of the ESP32 (shown in the log)
4. Enter the API key

### Verify Entities

After adding the device, the following entities are available:

- `cover.schlafzimmer` - Blind control
- `sensor.schlafzimmer_rssi` - Signal strength
- `text_sensor.schlafzimmer_status` - Status text

---

## 8. Adding Multiple Blinds

Add another cover entry for each additional blind:

```yaml
cover:
  - platform: elero
    name: "Schlafzimmer"
    blind_address: 0xa831e5
    channel: 4
    remote_address: 0xf0d008
    open_duration: 25s
    close_duration: 22s

  - platform: elero
    name: "Wohnzimmer Links"
    blind_address: 0xb912f3
    channel: 5
    remote_address: 0xf0d008
    open_duration: 30s
    close_duration: 27s

  - platform: elero
    name: "Wohnzimmer Rechts"
    blind_address: 0xc4a7d2
    channel: 6
    remote_address: 0xf0d008
    open_duration: 30s
    close_duration: 27s

# Diagnostic sensors (RSSI, status, problem, etc.) are created automatically
# by the cover/light blocks (auto_sensors: true is the default).
```

> **Note:** The component automatically staggers polling requests (5-second offset per blind) to avoid collisions on the RF channel.

---

## Next Steps

- [Configuration Reference](CONFIGURATION.md) - All parameters in detail
- [Example Configurations](examples/) - Templates for various scenarios
- [README](../README.md) - Overview and troubleshooting
