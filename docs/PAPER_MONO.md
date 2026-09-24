# PaperMono integration

The M5Stack PaperMono runs an optional native Elero frontend on its monochrome
480×800 touchscreen, using the onboard SX1262. It can operate without Wi-Fi, a
web server, Home Assistant, or a microSD card. Devices and groups live in the same
registry and NVS records used by the other adapters.

## Enabling it

Start with [the standalone configuration](../configs/config.paper-mono.yaml).
It contains the board's two SPI buses, I²C bus, radio power/reset configuration,
fonts, and frontend. Build with `mise run compile:paper-mono`.

`paper_mono:` enables board support. `elero_paper:` enables the Elero screen.
Neither is auto-loaded by `elero`, `elero_web`, `elero_nvs`, or `elero_mqtt`.
An `external_components.components` list makes modules available for resolution;
it does not enable their runtime components. The example leaves out that allowlist
so ESPHome can resolve internal dependencies automatically; only the configured
board/frontend and their dependencies are built.

The example uses `storage: false` by default. To enable microSD backup/restore:

```yaml
elero_paper:
  # Keep the board, display, and font references from the supplied configuration.
  storage: true
```

`mise run compile:paper-mono-sd` builds this option under a separate build name.
When disabled, the SD worker, FATFS configuration, and JSON backup codec are not
loaded by this frontend. When enabled, SD power and the worker start on first use.

The [networked example](../configs/config.paper-mono-web.yaml) adds Wi-Fi, the
web UI, and the Home Assistant native API. Replace its Wi-Fi placeholders before
deploying. Build with `mise run compile:paper-mono-web`. HA entities require saved
cover/light records; a sniffed remote alone is not an HA cover. Native API device
additions require a reboot, as on other boards.

## Ownership and data flow

The shared [frontend interface](FRONTENDS.md) defines command results, durable
configuration operations, and typed notifications for all UI modules.

| Module | Owns | Does not depend on |
|---|---|---|
| `elero` | RF transport, device state, NVS, groups, learn-in, channel command scheduling | Any frontend or PaperMono module |
| `elero_config` | Shared JSON snapshot parsing, import and export | Display, SD, HTTP |
| `paper_mono` | Power, GPIO expander, radio reset, touch access, card detection | Elero, fonts, web server |
| `paper_mono_display` | Framebuffer, panel SPI sequence, refresh timing, touch polling | Elero state or commands |
| `paper_mono_storage` | Optional SDMMC mounting and asynchronous file operations | Elero registry or JSON |
| `elero_paper` | Screens, input, edit drafts, observer, backup workflow | Web server |

RF events reach the registry before adapters. The PaperMono frontend explicitly
enables receiver discovery; other configurations retain their previous default.
Only valid targeted command envelopes infer receiver candidates. Candidates are
unsaved and do not initiate periodic polling. Saving promotes a candidate to a
managed device. Explicit user commands still use the core's command paths.

Channel operations are owned and processed by the hub, independently of panel
health. STOP replaces pending channel repeats. The UI distinguishes queued,
transmitted, and failed channel operations; transmitted means RF completion, not
an acknowledgement from the motor.

The display composes one element per loop and transfers the framebuffer in small
SPI chunks. Panel BUSY waits are cooperative. Hit targets become active after
the corresponding frame is visible, with an exception for an already visible STOP
control during navigation. No background task touches UI or registry objects.

SD jobs cross queues with exclusive ownership. The worker only reads/writes files;
snapshot serialization and registry imports occur on the main loop. The card is
never formatted. Exports use exclusive temporary files and FAT's no-overwrite
rename to publish complete backups. Failed writes remove their temporary file;
power interruption may leave an ignored `.tmp` file. File size is limited to
128 KiB and the browser to 64 JSON backups. Import merges rather than clearing
unrelated records. Individual capacity/member errors can produce partial imports;
the result reports imported and skipped records.

Device deletion prunes group references before removing the device and reports
preference-save failures. These are separate preference records, not an atomic
transaction: if a later save fails, earlier successful group changes remain and
the device is retained for retry. Flash durability additionally requires the
frontend's preference sync to succeed.

## Screens and behavior

- Channels 1–255, five per page, with up/stop/down icons; sniff or select a remote.
- Device discovery, filters, sorting, editing, persistence and controls.
- Groups and bulk commands. Light groups use light controls; Tilt is offered only
  when every saved member supports it.
- Hub settings, learn-in, raw command review, and a bounded RF packet viewer.
- Optional microSD backup/restore using the same snapshot format as the web UI.
- Text editing with Clear and Delete. Imported UTF-8 text is shortened and deleted
  by code point. Keyboard entry remains ASCII; additional display glyphs require
  configuring a font containing them.

Lists use pages rather than animated scrolling. The first refresh and periodic
full refreshes take longer than partial updates. Physical board pin assignments
and panel geometry are deliberately fixed; the board schema requires ESP32-S3
and the M5IOE1 address `0x4F`. The reset GPIO adapter exposes only PYG10.

## Verification and remaining hardware checks

CI compiles core-only, web-only, standalone PaperMono, SD-enabled PaperMono, and
combined frontend configurations using the pinned mise/uv toolchain. The optional
SD configuration also builds on Arduino, using the same implementation. Local checks
also inspect generated component sources and linked symbols for opt-in isolation.

Local ELF inspection on 2026-09-24 confirmed:

| Configuration | PaperMono board/frontend | SD worker | Snapshot codec |
|---|---|---|---|
| Core only | Absent | Absent | Absent |
| Web + NVS | Absent | Absent | Present |
| PaperMono, storage disabled | Present | Absent | Absent |
| PaperMono, storage enabled | Present | Present | Present |

ESPHome retains some generated source directories across configuration changes;
the generated umbrella header and linked ELF symbols are authoritative for what
the firmware includes. A fresh build name was also used for the no-SD check.
The local build matrix passes, as do the existing 36 Python tests, 60 registry
tests, and 47 command-queue tests, and Ruff/ty checks for the new component schemas.
The shared frontend operation changes also pass AsyncAPI schema validation.
The new operation-result and channel-notification paths still need focused
regression coverage once test edits are authorized. The full C++ suite is
blocked by the existing GPIO mock lacking the `GPIOPin` base used by the SX1262
reset adapter. Full-tree Python checks also have pre-existing test formatting and
dynamic-import resolution failures. Those files have not been changed without
the user's requested explicit test-edit authorization. CI wiring is present but
has not been run on a remote CI runner in this worktree.

Earlier hardware validation established RX, channel up/stop/down, readable pages,
touch navigation, name editing, icon controls, and NVS persistence. The subsequent
layer separation and asynchronous SD implementation still need a fresh USB hardware
check. SD insertion/removal, full-card/write-failure handling, backup round-trip,
and input responsiveness during SD operations have not yet been validated on a card.

See the [historical bring-up record](research/paper-mono-bringup.md) for pin sources,
original factory firmware backup, and earlier measurements. Those measurements do
not establish behavior of the later refactoring.
