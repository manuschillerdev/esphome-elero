# ESPHome compatibility

This branch requires ESPHome **2026.9.0 or newer**. Local development and CI
use the same pinned ESPHome release, Python, and tools from `pyproject.toml`,
`uv.lock`, and `.mise.toml`. Upgrade the ESPHome installation that builds your
firmware before updating the external component.

The nightly `Nightly ESPHome Compatibility` workflow runs at **03:23 UTC**
and can also be started manually. It resolves the newest published stable,
non-yanked ESPHome release from PyPI once, then runs the same frontend, Python,
C++ unit-test, and eight-firmware compile checks used by ordinary CI. Exact
version overlays leave the project's ESPHome pin and lockfile unchanged. A
new release requiring a newer Python fails explicitly rather than silently
testing an older compatible ESPHome release.

GitHub schedules run on the default branch; the workflow becomes active after
it is committed there. Failures appear in Actions and its check reports.

The nightly checks were verified locally against ESPHome **2026.9.1**: all eight
firmware configurations compiled, all 36 existing Python tests passed, and
Ruff lint/format, ty, and workflow syntax checks passed.

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

Native cover groups reserve another 16 cover entities. Their keys derive from
the stable group id in a separate namespace, so a group can share a display
name with a cover. Group position is the average member position; position
control requires travel durations on every member. Native group CRUD requires
a reboot, like individual native devices.

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

The frontend header is committed with the component, so a fresh Git installation
needs no frontend build or release-asset download. To compile a local checkout:

```sh
mise install
mise exec -- uv sync --locked
mise exec -- uv run esphome compile tests/test.esp32-nvs.yaml
mise exec -- uv run esphome compile tests/test.esp32-mqtt.yaml
mise exec -- uv run esphome compile tests/test.esp32-ard.yaml
```

After changing frontend source, regenerate and commit the bundled header:

```sh
mise exec -- pnpm --dir components/elero_web/frontend/app install --frozen-lockfile
mise exec -- pnpm --dir components/elero_web/frontend/app build
```

CI checks the regenerated header for drift. Firmware compile jobs consume the
committed header directly, without injecting a separately built artifact.

Validated with ESPHome 2026.9.0 and Python 3.12.14:

- The existing ESP32 NVS and MQTT fixtures compile with ESP-IDF 5.5.5.
- The existing ESP32 Arduino fixture compiles.
- Platform counts accumulate ordinary YAML entities alongside the reserved
  48 device covers, 16 group covers, and 48 light states. Automatic diagnostic
  sensors also contribute to their platform's count.
- Six existing CI configurations generate C++ successfully: ESP32-S3 Arduino,
  ESP32-S3 ESP-IDF, ESP32 Arduino, minimal, MQTT, and NVS.
- All 36 existing Python tests pass; existing test changes are mechanical formatting.
- All 482 existing C++ tests pass.
- Ruff lint/format, ty, and TypeScript 7.0.2 native-compiler checks pass.

Hardware verification remains outstanding. After flashing, verify native API
discovery and commands for both a cover and a light, light brightness changes,
WebSocket log forwarding, and persistence across reboot. Compilation alone
does not verify RF or Home Assistant behavior.
