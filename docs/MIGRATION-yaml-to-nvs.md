# Migrating from YAML-defined devices to NVS

Starting with v0.11.0 (RFC-002), Elero devices are no longer defined in YAML.
The `cover: - platform: elero` and `light: - platform: elero` schemas are
gone — every device lives in NVS and is managed via the web UI.

This guide walks you through the upgrade.

## TL;DR

```bash
# 1. Convert your existing YAML devices to a backup file
uv run scripts/migrate_yaml_to_json.py my-device.yaml -o backup.json

# 2. Edit my-device.yaml: remove cover:/light: blocks, keep elero: + elero_mqtt:/elero_nvs:

# 3. Flash the new firmware
uv run esphome run my-device.yaml

# 4. Open http://<host>/elero → Hub tab → Restore from backup → upload backup.json
```

The migration script is offline and only depends on `pyyaml`. It writes a
JSON file in the same `ConfigSnapshot` format the device's web UI emits,
so the round-trip is byte-stable for every device-config field.

## Why this change?

- **One source of truth.** Devices used to live in three places (YAML,
  NVS+native shells, NVS+MQTT) — that matrix is collapsed to one (NVS).
- **Backup/restore as a first-class feature.** The same JSON is used for
  chip-swap recovery, multi-hub provisioning, and reproducible setups.
- **No CI surprises.** `cover:`/`light:` YAML blocks now error out at
  config-validation time instead of silently ignoring the entries.

See [`docs/RFC-002-nvs-only-and-backup-restore.md`](RFC-002-nvs-only-and-backup-restore.md)
for the full design rationale.

## Step-by-step

### 1. Run the migration script

The script reads any YAML you point it at and emits a JSON file.

```bash
# Default output: <input>.backup.json
uv run scripts/migrate_yaml_to_json.py my-device.yaml

# Or pick a name explicitly
uv run scripts/migrate_yaml_to_json.py my-device.yaml -o my-backup.json
```

It will:

- Convert every `platform: elero` cover/light entry to a `DeviceSnapshot`.
- Carry over `elero_mqtt.device_name` as the hub's `name_override` so the
  HA gateway device keeps its display name.
- Skip non-elero entries silently (`platform: template`, etc.).
- Default missing optional fields (hop, payloads, msg_type, type2) to the
  exact same constants the firmware uses, so importing the file is
  byte-identical to the legacy behavior.

### 2. Edit your YAML

Delete the device blocks. Keep the hub block (`elero:`) and pick **one**
output adapter (MQTT or native + NVS).

**Before:**
```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

elero_mqtt:
  topic_prefix: elero
  device_name: "Living Room Hub"

cover:
  - platform: elero
    name: "Living Room Blind"
    dst_address: 0xa831e5
    src_address: 0xf0d008
    channel: 4
    open_duration: 25s
    close_duration: 22s
```

**After:**
```yaml
elero:
  cs_pin: GPIO5
  gdo0_pin: GPIO26

elero_mqtt:
  topic_prefix: elero
  device_name: "Living Room Hub"   # still used as the YAML default

elero_web:
  port: 80
```

If you want HA via the ESPHome native API instead of MQTT, swap
`elero_mqtt:` for `elero_nvs:` (both adapters consume the same NVS
device list).

### 3. Flash & restore

```bash
uv run esphome run my-device.yaml
```

After the device boots and joins WiFi, open `http://<host>/elero`,
navigate to the **Hub** tab, scroll to **Backup & Restore**, and click
**Restore from backup**. Pick the JSON file you generated in step 1.

The toast at the bottom of the screen reports `N added, M updated, K skipped`.
For a fresh chip with no NVS state you'll see all devices added.

### 4. (NATIVE_NVS only) Reboot to bind

If you chose `elero_nvs:`, the ESPHome native API binds entities at
`setup()` time. After importing, click the prompt to reboot so HA picks
up the newly-added cover/light entities.

MQTT mode picks up new devices immediately via HA discovery — no reboot
needed.

## Skipping the migration

If you'd rather start fresh: just remove the `cover:`/`light:` blocks
from YAML and re-add devices through the **Hub → Add Device** flow in
the web UI. Nothing else needs to change. (No data is lost in NVS — but
no automatic recovery either.)

## What about HA-side customisations?

Entity ID renames, area assignments, and other Home Assistant-side
customisations live in HA's own registry, not on the gateway. They are
unaffected by this migration but also not exported by the backup file.
Document them separately if you depend on them.

## Troubleshooting

**"Not a valid Elero backup snapshot" toast:** The file you uploaded
isn't a `ConfigSnapshot` JSON envelope. Re-run the migration script and
upload its output verbatim — don't hand-edit unless you've read the
schema in `components/elero_web/frontend/app/asyncapi.yaml`.

**"Unsupported snapshot_version N":** You're trying to import a backup
made by a newer firmware than what's running. Update the device firmware
or downgrade the backup file's `snapshot_version` (only safe if the
schema delta is purely additive — check the changelog).

**"No free slot":** You hit `MAX_DEVICES = 48`. Remove unused devices
in the UI, then retry the import.

**Hub display name didn't change after restore:** Check that your
backup's `hub.name_override` differs from the YAML default
(`elero_mqtt.device_name`). If they match, the snapshot intentionally
doesn't carry an override (importing one would just shadow the default).
