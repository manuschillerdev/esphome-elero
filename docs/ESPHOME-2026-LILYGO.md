# LilyGO LoRa32 with ESPHome 2026.8.2

This fork targets ESPHome **2026.8.2**. The primary configuration is an
ESP32-PICO-D4 with an onboard SX1276, native ESPHome API, NVS device storage,
and the `/elero` web UI. Use `esp32dev`, not an ESP32-S3 board definition.

## Build from the fork

In Home Assistant's ESPHome Device Builder, use:

```yaml
external_components:
  - source: github://aSauerwein/esphome-elero@dev
    refresh: 0s
```

This fork includes the generated web UI header, so no frontend build tools
are needed on Home Assistant. `refresh: 0s` fetches updates on each validation;
after testing, pin the source to a tested commit for reproducible builds.

For local builds, use Python 3.12–3.14 and `uv`. From the checkout root:

```bash
uv sync
```

Only after frontend changes, use Node.js 22 and pnpm 9 to rebuild the header.
In `components/elero_web/frontend/app`, run:

```bash
pnpm install --frozen-lockfile
pnpm build
```

This generates `components/elero_web/elero_web_ui.h` from this checkout's
frontend. Commit the updated header together with frontend updates. The
upstream `v0.9.0` release asset referenced by this checkout was unavailable
during testing, so direct installs use the included header. If the header
is absent, the component attempts the matching release download and stops during
code generation with the download URL and recovery command if it cannot
obtain the matching header. Failed downloads are not cached.

Use [`configs/config.lilygo-lora32-api-nvs.yaml`](../configs/config.lilygo-lora32-api-nvs.yaml).
Create `configs/secrets.yaml` locally with `wifi_ssid` and `wifi_password`.
The example uses DHCP and includes ESPHome OTA. If you copy the YAML elsewhere,
adjust the local external-component path to this fork's `components` directory.

From the checkout root:

```bash
uv run esphome compile configs/config.lilygo-lora32-api-nvs.yaml
uv run esphome run configs/config.lilygo-lora32-api-nvs.yaml --device /dev/ttyUSB0
```

Replace `/dev/ttyUSB0` with the board's serial port. Subsequent uploads can use
the board's IP address instead. No C++ standard overrides or deprecated
`platformio_options` are needed with this ESP-IDF configuration.

The credential-free CI build is:

```bash
uv run esphome compile tests/test.esp32-sx1276-idf.yaml
```

That test YAML uses a dummy Wi-Fi network; compile your own configuration
before flashing a network-connected device.

### Verified build milestone

Local checks with ESPHome 2026.8.2 / ESP-IDF 5.5.5:

| Check | Result |
|---|---|
| `tests/test.esp32-sx1276-idf.yaml` — SX1276, API, NVS, web | Passed |
| `tests/test.esp32-nvs.yaml` — CC1101, API, NVS, web | Passed |
| `tests/test.esp32-mqtt.yaml` — CC1101, MQTT, web | Passed |
| C++ unit tests | 430 passed |
| Python tests | 39 passed |

The LilyGO test image uses approximately 896 KB (49% of the app partition)
and 76 KB of static DRAM. Static DRAM figures do not include runtime heap
allocations. The final builds had no compiler format warnings; GPIO5's
configuration-time strapping-pin warning remains expected. CI also retains
the existing Arduino and ESP32-S3 compile jobs; those additional configurations
were not compiled locally for this milestone.

## Hardware verification

Compilation has been verified; the following checks still require the board.
Keep the board on a network reachable from Home Assistant and your browser.
Remote troubleshooting only requires shared logs initially; direct OTA/log
access from another machine requires IP connectivity to the board.

1. **Boot and radio initialization.** Capture serial logs from reset. Look for
   `SX1276 initialized, FSK mode`, its version value, Wi-Fi connection, and
   the web server starting. The configured pins are SCK=5, MOSI=27, MISO=19,
   CS=18, DIO0=26, RESET=23. GPIO5's strapping-pin warning is expected for this
   board wiring.
2. **Receive before transmitting.** Open `http://<board-ip>/elero` and inspect
   RF discovery while pressing UP, STOP, and DOWN on the already-paired
   physical remote several times. Record decoded source/destination addresses,
   channel, command, RSSI, and motor replies. Confirm repeated presses decode
   consistently. New remote-less learn-in is unnecessary for this workflow.
3. **Save and reboot.** Save the discovered shutter with a unique, non-empty
   name. Leave travel durations at zero for initial OPEN/CLOSE/STOP testing.
   Reboot after adding or editing devices in native API mode. Confirm the
   boot log reports the expected number of NVS cover entities and the device
   remains in the web UI.
4. **Home Assistant.** Add the board through the ESPHome integration if it is
   not automatically discovered. Confirm the named cover appears after reboot.
5. **Transmit and receive replies.** Test OPEN, STOP during movement, and CLOSE
   from Home Assistant. For each, capture the HA action, RF TX, physical motion,
   RF acknowledgment/status, and resulting HA state. Reboot once more and
   repeat to verify persistence.
6. **Backup.** Export the device configuration from the web UI after successful
   discovery and testing.

For diagnosis, share the boot log and a short log/RF capture spanning a remote
button press or HA command, plus the observed shutter movement.

## Compatibility implementation

- The web server uses ESPHome's context-pointer logger callbacks.
- Covers and lights are registered with the public
  `App.register_cover/light(entity, name, hash, fields)` overloads. Passing a
  zero hash lets ESPHome derive the object ID hash from the name, retaining
  name-based identity. Names are copied into firmware-lifetime storage because
  ESPHome retains a string reference. Use unique names; renaming changes identity.
- Covers need only one initialization call after NVS binding. Light states
  need the normal ESPHome component lifecycle, including loop enable/disable.
  Codegen therefore registers 48 light-state slots before `App.setup()`;
  the NVS adapter binds occupied slots after the registry restores devices,
  and the light states initialize next. Empty slots disable their loops and
  are never exposed as entities. This reserves RAM even in cover-only native
  configurations, preserving the registry's existing maximum capacity.
- Entity capacities are added through ESPHome's platform-count tracking, so
  ordinary YAML cover/light entities also fit alongside NVS entities.
- No runtime call to `App.register_component_()` is used. Component
  registration is handled by ESPHome's public Python codegen API.
- Radio changes in this compatibility update are limited to log formatting.

Hardware RX/TX, native entity enumeration after NVS restore, and live heap
headroom remain hardware acceptance checks, not conclusions from a successful
compile.
