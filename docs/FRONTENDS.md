# Frontend interface

Frontends read the registry, issue shared operations, and observe typed changes.
They own navigation, edit drafts, presentation, and refresh scheduling. Core code
does not depend on a frontend, display driver, JSON, or a transport.

## Queries and mutations

`DeviceRegistry` provides device/group lookup, iteration, and state snapshots.
Its frontend mutation methods are `save_device`, `delete_device`, `save_group`,
`delete_group`, and `save_hub_name`. These return `OperationResult` and perform
the preference sync themselves. Frontends must check `ok()` and present errors.

| Status | Meaning |
|---|---|
| `APPLIED` | The requested configuration operation completed and flash sync succeeded. |
| `QUEUED` | A command was accepted into the RF queue. |
| `REJECTED` | Validation, availability, or queue acceptance prevented completion of the request. |
| `FAILED` | A persistence operation failed; RAM or earlier group changes may already have applied. |

`command_device` routes covers, lights, tilt, and status requests. A failed queue
insertion is reported rather than presented as an accepted command.
`send_group_command` uses the existing group orchestration and reports queue
failure; earlier remote buckets may already be queued if a later bucket fails.
These results do not claim transmission or motor acknowledgement.

Low-level `upsert`, `persist`, and group methods remain staged operations for
setup, discovery, and batched imports. The shared snapshot importer syncs once
after applying its batch. Its result includes `persisted` and reports sync errors
in the existing error list. Imports and device/group deletion are not atomic
multi-record transactions.

Registry change notifications describe current RAM state and can precede flash
sync. Only an `APPLIED` operation result establishes durable success. If sync
fails, retrying a save also retries sync even when the value is unchanged.

## Notifications

Each frontend registers an `OutputAdapter`. Existing callbacks cover device,
state, config, group, hub-name, and RF changes. Callbacks run on the main loop;
observers must copy data they retain and must not issue commands reentrantly.
PaperMono marks a screen dirty; the web adapter serializes notifications.

Channel control uses `Elero::request_channel_command`. Its result contains a
nonzero, hub-local operation ID, valid until reboot. `on_channel_command_result`
reports `QUEUED`, then `TRANSMITTED`, `FAILED`, or `CANCELLED`. STOP replaces a
pending channel operation and cancels its ID. A rejected request leaves the
previous operation intact. `TRANSMITTED` means the radio finished sending the
repeat sequence, not that a motor acknowledged or moved. The core owns progress
even if a frontend or display fails.

There is no generic UI superclass or event bus. Hardware modules remain separate
from presentation, and each frontend is enabled explicitly in ESPHome YAML.
Learn-in continues to expose its existing core state machine; per-device live
state continues through the registry's snapshot/change notifications.

## WebSocket mapping

Existing request and live-event formats remain supported. Shared operations send
`operation_result` on success and the existing `error` event on failure, with
`operation`, `operation_id`, `status`, and `msg` fields. Existing browser clients
ignore additive success events and continue to consume live updates and errors.

The additive `channel_cmd` request takes `src_address`, numeric `channel` (1–255),
and `action` (`up`, `stop`, or `down`). Channel progress is broadcast as
`channel_command_result` with `operation_id`, `remote`, `channel`, `command`, and
`status`. The queued notification may arrive before the request's result. IDs
and target fields let clients associate progress without storing connection
pointers in core code. Reconnecting clients receive the existing state/config
snapshot; operation notifications are not a replay log.
