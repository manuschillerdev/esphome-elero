# Backup &amp; Restore

The web UI's **Backup &amp; Restore** card lets you download every NVS-persisted
device — plus your hub overrides — as a single JSON file, and restore it on the
same or a different hub.

This is the supported recovery path for:

- **Chip-swap recovery.** A blown ESP32 means a fresh NVS partition. Without a
  backup you'd have to re-add every device by hand.
- **Reproducible setups.** Multi-hub deployments (vacation home, second floor,
  staging vs. prod) where you want one source of truth.
- **Pre-flash safety net.** Always download a backup before flashing major
  firmware updates — NVS layout is versioned, but mistakes happen.

> **Heads up:** the JSON is plain-text and unencrypted. RF addresses are not
> secrets, but if you'd rather not check the file into a public repo, treat it
> like any other home-network artifact.

---

## Downloading a backup

1. Open `http://<device-ip>/elero` and switch to the **Hub** tab.
2. Scroll to **Backup &amp; Restore**.
3. Click **Download backup**.

Your browser receives a file named `<device-name>-backup-<timestamp>.json`
(e.g. `elero-gateway-backup-2026-05-11_09-30-00.json`). A toast confirms how
many devices were exported.

The download is a one-shot WebSocket round-trip (`export_config` →
`config_snapshot`); it does not affect device state and you can do it as often
as you like.

### What's included

| Block | Contents |
|---|---|
| `snapshot_version` | Envelope format version (currently `1`). |
| `exported_at` | `millis()` since the source device booted (informational only). |
| `exporter` | Source `device` name + component `version` string. |
| `hub.name_override` | Persisted hub display name override. **Omitted** when no override is set, so the imported snapshot won't shadow the YAML default. |
| `devices[]` | Every active, *persisted* device (`updated_at != 0`). Auto-discovered remotes you haven't saved yet are excluded — they're recreated on the new hub the next time their physical remote transmits. |

### What's NOT included

- Hub YAML config (pins, frequency registers, output adapter choice). Those
  live in the firmware itself — re-flashing the same YAML brings them back.
- HA-side customisations (entity_id renames, area assignments, automations).
  Those live in Home Assistant's own registry and aren't visible from the gateway.
- Live state (positions, RSSI, last-seen timestamps). Position derives from
  the FSM at runtime; RSSI is the latest RF observation. Both are recomputed
  after restore.

---

## Restoring a backup

1. Hub tab → **Backup &amp; Restore** → **Restore from backup**.
2. Pick the JSON file. The browser parses it locally first; an obviously
   invalid file is rejected before anything is sent to the device.
3. The device replies with a summary toast: `N added, M updated, K skipped`.

Behavior:

- **`(device_type, dst_address)` already exists** → update in place. The
  existing slot's RF state and Published cache are preserved; only the config
  fields change.
- **New device** → claim a free NVS slot.
- **No free slot** → that entry is skipped, counted in `skipped`, and reported
  in the per-entry `errors[]` array (visible in the device console log).
- **Hub override** present → applied via the same path as the in-UI rename.

Adapters react automatically:

- **MQTT mode** (`elero_mqtt:`): each upserted device fires HA discovery
  topics immediately. Devices appear in HA without intervention.
- **Native API mode** (`elero_nvs:`): ESPHome cannot register entities after
  the API connection is up. The toast invites you to reboot — after the
  reboot, HA sees the restored entities.

---

## Snapshot file format

```json
{
  "snapshot_version": 1,
  "exported_at": 1715000000000,
  "exporter": { "device": "elero-gateway", "version": "0.10.0" },
  "hub": { "name_override": "Living Room Hub" },
  "devices": [
    {
      "device_type": "cover",
      "dst_address": "0xa831e5",
      "src_address": "0xf0d008",
      "channel": 4,
      "name": "Living Room",
      "enabled": true,
      "open_duration_ms": 25000,
      "close_duration_ms": 22000,
      "supports_tilt": true,
      "ha_device_class": 0,
      "hop": "0x0a",
      "payload_1": "0x00",
      "payload_2": "0x04",
      "msg_type": "0x6a",
      "type2": "0x00"
    }
  ]
}
```

The schema is the source-of-truth `ConfigSnapshot` definition in
[`components/elero_web/frontend/app/asyncapi.yaml`](../components/elero_web/frontend/app/asyncapi.yaml).
TypeScript types are codegenerated from there into `src/generated/`.

### Versioning

`snapshot_version` is bumped only on **breaking** schema changes. Additive
fields (new optional keys) do **not** bump the version — older firmware will
ignore unknown keys. The import handler rejects any version it doesn't
recognise rather than silently misinterpreting fields.

If you need to import an old backup into newer firmware: that always works
(the format is forward-compatible by design). If you need to import a *newer*
backup into older firmware: update the firmware first.

### Hand-editing

The format is plain JSON; you can hand-edit it. Useful tweaks:

- **Renaming devices in bulk** before restoring on a new hub.
- **Toggling `enabled`** to silence a device in HA without removing it.
- **Adjusting `open_duration_ms` / `close_duration_ms`** if you re-measured
  travel time without going through the UI.

If you rename a key or change types, the import handler's per-entry validator
rejects that device and reports the error — the rest of the import still
applies.

---

## Migrating from YAML-defined devices

If you're upgrading from a firmware version where blinds were defined under
`cover: - platform: elero` in YAML, you cannot use **Download backup** to
recover them — the gateway doesn't know about YAML devices anymore.

Instead, run the offline migration script *before* upgrading:

```bash
uv run scripts/migrate_yaml_to_json.py old-config.yaml -o backup.json
```

…then flash the new firmware and use **Restore from backup** with
`backup.json`. Full step-by-step: [`MIGRATION-yaml-to-nvs.md`](MIGRATION-yaml-to-nvs.md).

---

## Troubleshooting

### "Not a valid Elero backup snapshot" toast

The file you uploaded doesn't match the snapshot envelope schema (missing
`snapshot_version`, `devices[]`, etc.). The browser checks shape locally
*before* sending anything to the device — re-export the backup or re-run the
migration script to generate a valid file.

### "Unsupported snapshot_version N (expected 1)"

You're trying to import a backup made by a newer firmware than what's running.
Update the firmware on the target device, then retry.

### "No free slot"

You hit `MAX_DEVICES = 48`. Remove unused devices in the UI (or in the JSON
before importing), then retry. The summary's `errors[]` list which entries
were skipped.

### "Import not supported in native mode" (MQTT-style error)

You're seeing this only on a hub where neither `elero_mqtt:` nor `elero_nvs:`
is configured (i.e. NVS persistence isn't enabled at all). Add one of the two
adapters, reflash, retry.

### Restore succeeded but HA doesn't see new devices

You're in **Native API mode** (`elero_nvs:`). ESPHome can only register
entities at boot — reboot the device and HA will pick them up. The web UI
shows a banner prompting for the reboot after a successful import in this mode.

### Hub display name didn't change

Either:

- The snapshot's `hub.name_override` matched the current YAML default
  (`elero_mqtt.device_name`). When they're equal, no override is persisted —
  the YAML default is already in effect.
- The snapshot didn't include a `hub.name_override` (the source device was
  using its YAML default, so the export deliberately omitted it).

To force a name change, set it in the **Display Name** field on the Hub tab,
or hand-edit the JSON to add `hub.name_override`.

---

## See also

- [`MIGRATION-yaml-to-nvs.md`](MIGRATION-yaml-to-nvs.md) — upgrading from YAML-defined devices.
- [`CONFIGURATION.md`](CONFIGURATION.md) — full hub/adapter parameter reference.
- [`RFC-002-nvs-only-and-backup-restore.md`](RFC-002-nvs-only-and-backup-restore.md) — the design rationale.
- [`asyncapi.yaml`](../components/elero_web/frontend/app/asyncapi.yaml) — authoritative WebSocket protocol spec, including all backup/restore message shapes.
