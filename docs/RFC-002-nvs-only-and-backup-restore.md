# RFC-002: NVS-Only Devices + Backup/Restore

**Status:** Proposed
**Date:** 2026-05-11
**Branch:** `feat/backup-restore` (off `dev`, post-merge of PR #48)
**Predecessor context:** PR #48 (HA-native naming + hub_name override) is merged. `NvsHubConfig` exists. `OutputAdapter::on_hub_config_changed()` exists. `DeviceRegistry::set_hub_name_override()` is the template for hub-level overrides.

---

## Goal

Eliminate YAML-defined devices entirely. **All devices live in NVS**, manageable via the web UI, with a JSON export/import flow that supports chip-swap recovery and reproducible setups.

After this RFC, the only way a user adds an Elero device is through the web UI (or by importing a JSON config). YAML configures the **hub** (radio pins, frequency, mode selector) — nothing else.

---

## Why

1. **One source of truth.** Today devices can live in YAML (native mode), NVS+native shells (NATIVE_NVS), or NVS+MQTT. Three modes × two device sources is a state-space explosion the user has flagged before.
2. **Backup/restore is the killer feature** for chip-swap recovery, multi-hub provisioning, and reproducibility. ESPHome has no native NVS backup ([RFC-001 research](RFC-002-nvs-only-and-backup-restore.md#appendix-a-research-summary) — feature reqs [#1423](https://github.com/esphome/feature-requests/issues/1423), [#3015](https://github.com/esphome/feature-requests/issues/3015) open).
3. **Future src_address override** (chip-swap identity restore) only works if the device configs they go with can also be restored. Backup/restore is the prerequisite.
4. **Reduces matrix in tests, docs, frontend logic.** No more `mode === 'native'` branches.

---

## Scope

### In scope

- Remove `cover: - platform: elero` and `light: - platform: elero` YAML schemas.
- Remove `HubMode::NATIVE` and `DeviceRegistry::register_device()` (the YAML-only insertion path).
- Add WebSocket `export_config` + `import_config` messages with JSON payload.
- Add frontend Download/Upload UI in the hub panel.
- Migration tooling: Python script that converts a legacy `cover:`/`light:` YAML to the new JSON import format.
- Rewrite tests, docs, `example.yaml`.

### Out of scope (separate PRs after this lands)

- MAC-derived `derived_src_address()` / `src_override` (uses the export/import path; queue after).
- Pairing wizard ("learn-in" workflow).
- Auto-backup to MQTT retained topic (optional follow-up).

### Non-goals

- Backwards compatibility with YAML device configs at runtime. Users either migrate via the script or re-add via the UI.

---

## Final architecture

```
┌─────────────────────────────────────────────────┐
│  YAML (compile-time)                            │
│  ─────────────────                              │
│  • elero:           ← hub: pins, freq, version  │
│  • elero_mqtt: OR                               │
│    elero_nvs:       ← picks the output adapter  │
│  • elero_web:       ← UI + WebSocket            │
└─────────────────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────┐
│  NVS (runtime)                                  │
│  ─────────────                                  │
│  • NvsHubConfig     ← hub_name override, …      │
│  • NvsDeviceConfig × MAX_DEVICES (48)           │
└─────────────────────────────────────────────────┘
                       │ DeviceRegistry restore_all()
                       ▼
                  (devices)
                       │
        ┌──────────────┴──────────────┐
        ▼                             ▼
   MqttAdapter                  NvsAdapter
   (HA MQTT discovery)          (creates ESPHome
                                 cover/light entities
                                 from NVS at boot)
```

`HubMode` enum collapses to `{ MQTT, NATIVE_NVS }`. Both are NVS-sourced; the only difference is which adapter publishes to HA. (We could also drop the enum entirely and have adapters self-identify — see [§ Open Questions](#open-questions).)

---

## Implementation phases (single PR, multiple commits)

### Phase 1 — Backup/restore foundation (no removals)

**Files added/changed:**
- `components/elero_web/elero_web_server.{h,cpp}`:
  - New handler `handle_export_config_(c, root)` — iterate `registry_->for_each_active()`, serialize each `NvsDeviceConfig` + `NvsHubConfig` snapshot into a JSON envelope, send back to caller (or trigger a download via a synthetic HTTP route).
  - New handler `handle_import_config_(c, root)` — validate envelope version, call `registry_->upsert()` per device, call `registry_->set_hub_name_override()` for hub fields. Reply with summary (added/updated/skipped counts) or error array.
  - Dispatch in `handle_ws_message`: `if (type == "export_config") ...` / `if (type == "import_config") ...`.
- `components/elero_web/frontend/app/asyncapi.yaml`:
  - New messages: `exportConfigMessage` (request) / `configSnapshotEvent` (response), `importConfigMessage` / `importResultEvent`.
  - New schemas: `ConfigSnapshot` (version, exported_at, hub: HubSnapshot, devices: DeviceSnapshot[]), `DeviceSnapshot` (mirrors NvsDeviceConfig fields).
  - Bump AsyncAPI `info.version` (minor bump — additive).
- `components/elero_web/frontend/app/src/components/hub-panel.tsx`:
  - Add "Backup" card with two buttons: **Download backup** (triggers `export_config`, saves blob as `.json`) and **Restore from backup** (file picker → `import_config`).
  - Show summary toast on import response.
- `components/elero_web/frontend/app/src/ws.ts`:
  - `sendExportConfig()`, `sendImportConfig(snapshot)`, event handlers for `config_snapshot` / `import_result`.
- `components/elero_web/frontend/app/src/store.ts`:
  - `onConfigSnapshot(data)` — triggers download via Blob + anchor click.
  - `onImportResult(data)` — sets a toast signal.

**Tests:**
- Unit tests in `tests/unit/test_device_registry.cpp` for the serialization round-trip (build snapshot → parse → upsert → verify identical state).
- E2E manual test: download, edit JSON, import, verify devices reappear.

**Done when:**
- Both modes (MQTT + NVS) work unchanged.
- Round-trip works: export → erase NVS → import → identical state.

### Phase 2 — Add YAML→JSON migration tool

**File added:**
- `scripts/migrate_yaml_to_json.py` — reads a YAML file, finds `cover: - platform: elero` and `light: - platform: elero` entries, emits a `ConfigSnapshot` JSON file.
  - Use `uv run` per project convention.
  - Validates each entry against the same schema the server enforces.
  - CLI: `uv run scripts/migrate_yaml_to_json.py example.yaml -o backup.json`.
- `docs/MIGRATION-yaml-to-nvs.md` — step-by-step upgrade guide:
  1. Run migration script on existing YAML → produces `backup.json`.
  2. Edit YAML: remove `cover:` / `light:` blocks, add `elero_nvs:` (or `elero_mqtt:`).
  3. Flash new firmware.
  4. Open web UI → Restore → upload `backup.json`.
  5. Verify devices appear; reboot if NATIVE_NVS mode (registry note: NVS-API-bridge bind happens at setup).

**Tests:**
- `tests/python/test_migration.py` — fixture YAML files in `tests/python/fixtures/`, verify expected JSON output.

**Done when:**
- Sample `example.yaml` migrates cleanly.
- All 12+ legacy test YAMLs migrate without errors.

### Phase 3 — Remove YAML platforms

**Files deleted:**
- `components/elero/cover/__init__.py`
- `components/elero/light/__init__.py`
- (Keep `EspCoverShell` / `EspLightShell` C++ classes — `NvsAdapter` still creates them dynamically.)

**Files changed:**
- `components/elero/esp_cover_shell.h` — remove the `setup()` codepath that calls `registry_->register_device(cfg_)`. The shell is now only constructed by `NvsAdapter` with a slot index.
- `components/elero/esp_light_shell.h` — same.
- `components/elero/device_registry.{h,cpp}`:
  - Delete `DeviceRegistry::register_device()`.
  - Delete `RegisterDevice_RejectsWhenNvsEnabled` test (the inverse-guard becomes unreachable).
- `components/elero/device_type.h`:
  - Remove `HubMode::NATIVE`. Rename remaining `NATIVE_NVS` → `NATIVE` (or drop enum entirely; see Open Questions).
- `components/elero/device_registry.h:194` — change `mode_{HubMode::NATIVE}` default.

**Tests:**
- `tests/python/test_cover_validation.py` — delete or rewrite (it validates the deleted platform schema).
- Verify build via fresh `uv run esphome compile` of every test YAML.

**Done when:**
- `uv run esphome compile tests/test.esp32-mqtt.yaml` clean.
- `uv run esphome compile tests/test.esp32-nvs.yaml` clean.
- Old YAMLs that still use `platform: elero` fail with a clear config error (not a cryptic codegen crash).

### Phase 4 — Rewrite tests & docs

**Files rewritten:**
- `tests/common.yaml` — strip `cover:` / `light:` blocks, leave only hub + `elero_web`. Reference NVS adapter.
- `tests/test.esp32-{custom-freq,lights-only,multi-cover,api,sx1276,sx1276-idf}.yaml` — either delete (if redundant with mqtt/nvs configs) or rewrite as hub-only compile checks. Keep `api`/`sx1276` variants if they cover unique platform paths.
- `example.yaml` — full rewrite: hub + `elero_nvs:` + `elero_web:`. Plus a comment block: "Add devices via the web UI at http://<host>/elero or import a backup."
- `README.md`:
  - Modes table: 3 → 2 (MQTT, NATIVE_NVS).
  - Quick-start: device management via web UI.
  - Migration section linking to `docs/MIGRATION-yaml-to-nvs.md`.
- `docs/CONFIGURATION.md` — strip cover/light platform docs; replace with "Adding devices" via UI/import.
- `docs/INSTALLATION.md` — drop YAML device steps.
- `AGENTS.md`:
  - Three Operating Modes table → Two Operating Modes.
  - Remove references to YAML-defined devices.
  - Add note: backup/restore is the supported recovery path.

**Done when:**
- All compile tests pass.
- All unit tests pass (~394 + new round-trip tests).
- Python tests pass.

---

## Migration story (user-facing)

Old config (deleted):
```yaml
cover:
  - platform: elero
    dst_address: 0xa831e5
    src_address: 0xf0d008
    channel: 4
    name: "Living Room"
```

New flow:
1. **Before upgrade**: `uv run scripts/migrate_yaml_to_json.py current.yaml -o backup.json`
2. **Edit YAML**: replace device blocks with `elero_nvs:` (or keep `elero_mqtt:` if using MQTT).
3. **Flash** new firmware.
4. **Open** `http://<host>/elero` → **Restore from backup** → upload `backup.json`.
5. **Done**. Devices reappear; if NATIVE_NVS, the UI prompts for a reboot to bind ESPHome entities.

If a user skips the migration script and just removes their YAML blocks: they boot with an empty registry. They can re-add devices manually via the UI's existing Add-Device flow. No data loss in NVS — but no automatic recovery either.

---

## JSON snapshot format

Versioned envelope, additive-only changes:

```json
{
  "snapshot_version": 1,
  "exported_at": 1715000000000,
  "exporter": { "device": "elero-gateway", "version": "0.10.0" },
  "hub": {
    "name_override": "Living Room Hub"
  },
  "devices": [
    {
      "type": "cover",
      "dst_address": "0xa831e5",
      "src_address": "0xf0d008",
      "channel": 4,
      "name": "Living Room",
      "open_duration_ms": 25000,
      "close_duration_ms": 22000,
      "supports_tilt": true,
      "enabled": true,
      "ha_device_class": 0
    }
  ]
}
```

Server-side import:
- Reject unknown `snapshot_version` with a clear error.
- For each device, call existing `parse_device_config_()` + `registry.upsert()`.
- For hub block, call `registry.set_hub_name_override()` (extensible to future fields).
- Reply with `{ added: N, updated: M, skipped: K, errors: [{idx, msg}, ...] }`.

---

## Files touched (estimated)

| Layer | Files | Notes |
|---|---|---|
| C++ — server | `elero_web_server.{h,cpp}` | export/import handlers |
| C++ — registry | `device_registry.{h,cpp}` | delete `register_device`; serialize/deserialize helpers |
| C++ — types | `device_type.h`, `device_registry.h` | remove `HubMode::NATIVE` |
| C++ — shells | `esp_cover_shell.h`, `esp_light_shell.h` | remove YAML setup path |
| Python — codegen | `cover/__init__.py`, `light/__init__.py` | DELETE |
| AsyncAPI | `asyncapi.yaml` | 2 new messages, 4 new schemas |
| Frontend | `hub-panel.tsx`, `store.ts`, `ws.ts`, `models/hub.ts` | UI + actions |
| Tests | `common.yaml`, all `test.esp32-*.yaml`, `tests/python/*.py`, `tests/unit/test_device_registry.cpp` | rewrites + new round-trip tests |
| Docs | `README.md`, `docs/CONFIGURATION.md`, `docs/INSTALLATION.md`, `AGENTS.md`, `example.yaml`, new `docs/MIGRATION-yaml-to-nvs.md` | content rewrites |
| Scripts | `scripts/migrate_yaml_to_json.py` (new) | one-shot migration |

Rough size: ~600–900 lines added, ~500–800 deleted (after Phase 3).

---

## Open questions

1. **Drop `HubMode` enum entirely?**
   - With YAML mode gone, the only signal is "which adapter is configured." MqttAdapter and NvsAdapter could each call `registry.add_adapter(this)` and the registry asks adapters for their identity. The `crud` flag stays as a registry property (`nvs_enabled_`). The `mode` field in the web config event becomes derived from loaded adapters.
   - **Recommendation:** drop the enum. Reduces surface area. Frontend still gets a `mode` string in `HubConfig` (computed server-side from loaded adapters) so the UI doesn't have to know.

2. **Import behavior on duplicate addresses?**
   - Same `(type, dst_address)` as existing device: **update in place** (matches `upsert()` semantics). Reflected in the `updated` counter.
   - User probably wants this; document it.

3. **Encryption / signing of the backup file?**
   - Probably not for v1 — Elero RF protocol itself is unencrypted. Could add HMAC + a per-hub key in v2.

4. **Auto-export on every NVS change?**
   - Could publish a retained MQTT message with the full snapshot on every upsert. Self-healing backup.
   - **Defer to follow-up RFC.** Big change, opinionated, and depends on user's MQTT broker retention policy.

5. **What about user customisations in HA?** (entity_id renames, area assignments)
   - These live in HA's own registry, untouched by our backup. Documented limitation.

---

## Test plan

- **Unit:** new round-trip tests in `test_device_registry.cpp` — serialize snapshot, clear registry, deserialize, verify field-by-field equality.
- **Python:** `test_migration.py` — fixture YAMLs → expected JSON outputs.
- **Compile:** every remaining `test.esp32-*.yaml` builds cleanly via `uv run esphome compile`.
- **Manual on hardware (release gate):**
  1. Fresh hub, no NVS state. Add 3 devices via UI.
  2. Download backup. Inspect JSON.
  3. `esptool.py erase_flash` to simulate chip swap.
  4. Re-flash, open UI, restore backup, verify all 3 devices reappear with identical state.
  5. Verify HA picks them up via MQTT discovery (MQTT mode) or via ESPHome native API (NATIVE_NVS mode).
- **Regression:** run the existing 394 unit tests; expect no regressions.

---

## Risk register

| Risk | Mitigation |
|---|---|
| Existing users break on upgrade | Migration script + clear release notes + version bump to 0.11.0 (breaking) |
| Frontend bundle size growth | JSON snapshot UI is minimal (file picker + parse + send); negligible |
| NVS slot exhaustion on import | Same `MAX_DEVICES=48` cap as today; import returns `skipped` with reason |
| Corrupted backup files | Versioned envelope + schema validation + per-device error array; partial imports allowed |
| Race during import (TX in flight) | Apply imports through the registry's existing `upsert()` which is main-loop-safe |

---

## Order of operations for the implementer

This is the recommended commit sequence on `feat/backup-restore`:

1. `feat: add config snapshot export over WebSocket` — Phase 1 server side
2. `feat: add config snapshot import over WebSocket` — Phase 1 server side
3. `feat: frontend backup/restore UI` — Phase 1 frontend
4. `test: round-trip serialization tests` — Phase 1 tests
5. `feat: yaml-to-json migration script` — Phase 2
6. `docs: migration guide` — Phase 2 docs
7. `chore!: remove YAML cover/light platforms` — Phase 3 (BREAKING)
8. `chore!: drop HubMode::NATIVE and register_device` — Phase 3
9. `test: rewrite compile tests for NVS-only` — Phase 4 tests
10. `docs: rewrite README, CONFIGURATION, AGENTS for NVS-only` — Phase 4 docs
11. `chore: bump version to 0.11.0` — release-please will pick up the `!` markers

Each commit should leave the project in a buildable + passing-tests state where possible. Commits 7–8 are necessarily breaking; mark them with `!` so release-please bumps the major.

---

## Appendix A — Research summary (carried from RFC discussion)

- ESPHome has no native NVS backup/restore. Open feature requests: [#1423](https://github.com/esphome/feature-requests/issues/1423), [#3015](https://github.com/esphome/feature-requests/issues/3015).
- ESP-IDF provides `nvs_entry_find` / `nvs_entry_info` iterator API; values must be fetched per-type. Doable but awkward.
- Espressif's offline `nvs_partition_gen.py` / `nvs_partition_tool.py` work on raw partition blobs (USB serial only — not viable for runtime web UI).
- The schema-aware JSON path (this RFC) is idiomatic for this codebase because `DeviceRegistry::for_each_active()` + existing CRUD + AsyncAPI already do 80% of it.

## Appendix B — Pre-existing facts the implementer should know

- `NvsHubConfig` struct exists; key constant `nvs_pref_key::HUB`. To add fields, bump `NVS_HUB_CONFIG_VERSION` and handle old records on load (current `is_valid()` is version-strict — relax to "version <= current" + per-field migration).
- `DeviceRegistry::init_preferences()` runs BEFORE `setup_adapters()` (fixed in PR #48). Backup load can happen in either place.
- `OutputAdapter::on_hub_config_changed()` exists; reuse for import-triggered republish.
- AsyncAPI types are codegen'd via `pnpm generate:types` — always update the spec first, then regenerate.
- Frontend pattern: `useSignalEffect` for syncing input drafts from server state (see `HubInfoCard` post-PR #48).
- Build: `uv run esphome compile <yaml>`, not `esphome compile`. Unit tests: `cd tests/unit && cmake --build build && ctest --test-dir build`.
- Branch convention used by automation: `pi/<session-id>`. This branch is named manually: `feat/backup-restore`.
