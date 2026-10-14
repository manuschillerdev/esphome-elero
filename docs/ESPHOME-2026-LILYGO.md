# LilyGO LoRa32 with native API and ESP-IDF

The [example configuration](../configs/config.lilygo-lora32-api-nvs.yaml)
uses the onboard SX1276, ESPHome native API, NVS device storage, and `/elero`
web UI. It targets the ESP32 LoRa32 board with `esp32dev` and ESP-IDF.

## Local build

Create `configs/secrets.yaml` with `wifi_ssid` and `wifi_password`, then run
from the repository root:

```bash
uv sync --locked
uv run esphome compile configs/config.lilygo-lora32-api-nvs.yaml
uv run esphome run configs/config.lilygo-lora32-api-nvs.yaml --device /dev/ttyUSB0
```

Replace the serial port with your board's port. The example includes OTA and
uses DHCP. Its local component path assumes this repository layout.

## Home Assistant Device Builder

When copying the example into Device Builder, replace its local component
source with:

```yaml
external_components:
  - source: github://manuschillerdev/esphome-elero@dev
```

The frontend is bundled with the component revision; a frontend build is
needed only when changing its source. Pin a tested commit for reproducible
installations.

## Radio wiring and device setup

The onboard connections are SCK=GPIO5, MOSI=GPIO27, MISO=GPIO19, CS=GPIO18,
DIO0=GPIO26, and RESET=GPIO23. GPIO5's strapping warning is expected for this
board's fixed wiring. See the [LoRa32 board guide](devices/lilygo-lora32-sx1276.md)
for radio details.

Open `http://<board-ip>/elero` to discover and save devices. In native API mode,
reboot after device configuration changes so Home Assistant entities are
registered. Export the device configuration from the web UI for backup.

The credential-free native API compile fixture is
`tests/test.esp32-sx1276-idf.yaml`. Compilation does not establish hardware RX/TX
or Home Assistant operation.
