# ESPHome compatibility

This branch requires ESPHome **2026.9.0 or newer**. Local development and CI
use the same pinned ESPHome release, Python, and tools from `pyproject.toml`,
`uv.lock`, and `.mise.toml`. Upgrade the ESPHome installation that builds your
firmware before updating the external component.

## Native API and NVS

Devices still live in NVS and are managed through the web UI or
`import_config`. Existing NVS records and backup files do not need conversion.
Adding, removing, or renaming native API entities still requires a reboot.
MQTT mode keeps its immediate device updates.

ESPHome removed `EntityBase::set_name()` and runtime
`Application::register_component()`. The native adapter now uses the public
entity registration overloads, which accept the name and derive the same
name-based object ID hash. Names are copied into firmware-lifetime storage so
editing the registry cannot invalidate the strings held by ESPHome.

Light states need ESPHome's component lifecycle for their loop, transitions,
and scheduler. Code generation therefore registers 48 light-state component
slots, matching the registry's device limit, before application setup. After
NVS restoration, the adapter binds occupied light slots and registers only
active, enabled devices as entities. Unoccupied slots disable their loops and
are never exposed to Home Assistant. Cover shells have no periodic work and
are initialized directly when bound.

Cover and light entity capacities are added through ESPHome's platform counts,
so ordinary YAML entities can coexist with the NVS devices. Automatically
created diagnostic sensors use the current entity registration API and also
contribute to the sensor capacity.

WebSocket log forwarding uses ESPHome's context-pointer log callback API.

These changes adapt the native registration approach contributed in
[PR #65](https://github.com/manuschillerdev/esphome-elero/pull/65) and the logger
migration in [PR #58](https://github.com/manuschillerdev/esphome-elero/pull/58),
addressing [issue #59](https://github.com/manuschillerdev/esphome-elero/issues/59).

## Local verification

On macOS, use mise 2026.9.11 or newer (`mise self-update 2026.9.11` for the
version used here). Older mise releases may not resolve the Aqua tool registry.
The project explicitly pins `aqua:Kitware/CMake` 4.2.3 to avoid the legacy asdf
plugin's [app-bundle copying issue](https://github.com/jdx/mise/discussions/4866),
which can produce the macOS “CMake.app is damaged” dialog. The fresh Aqua bundle's
signature and `cmake --version` were verified in a normal host process. This
setup keeps Gatekeeper enabled; existing legacy tool installations can remain
because the project selects the explicit Aqua backend.

Build the frontend header before compiling a local checkout:

```sh
mise install
mise exec -- uv sync --locked
mise exec -- pnpm --dir components/elero_web/frontend/app install --frozen-lockfile
mise exec -- pnpm --dir components/elero_web/frontend/app build
mise exec -- uv run esphome compile tests/test.esp32-nvs.yaml
mise exec -- uv run esphome compile tests/test.esp32-mqtt.yaml
mise exec -- uv run esphome compile tests/test.esp32-ard.yaml
```

Validated with ESPHome 2026.9.0 and Python 3.12.14:

- The existing ESP32 NVS and MQTT fixtures compile with ESP-IDF 5.5.5.
- The existing ESP32 Arduino fixture compiles.
- A temporary configuration combining NVS with ordinary YAML cover, light,
  and sensor entities compiles, including all ten automatic diagnostic sensors.
  Generated entity capacities are 49 covers, 49 lights, and 11 sensors.
- Six existing CI configurations generate C++ successfully: ESP32-S3 Arduino,
  ESP32-S3 ESP-IDF, ESP32 Arduino, minimal, MQTT, and NVS.
- All 36 existing Python tests pass. No tests were added or changed.

Hardware verification remains outstanding. After flashing, verify native API
discovery and commands for both a cover and a light, light brightness changes,
WebSocket log forwarding, and persistence across reboot. Compilation alone
does not verify RF or Home Assistant behavior.
