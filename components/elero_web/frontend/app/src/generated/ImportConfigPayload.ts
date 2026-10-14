import {ConfigSnapshot} from './ConfigSnapshot';
/**
 * Restore from a snapshot. Existing devices with the same
 * `(device_type, dst_address)` are updated in place; new devices
 * are added. Hub overrides (e.g., `name_override`) are applied if present.
 */
interface ImportConfigPayload {
  'type': 'import_config';
  /**
   * Versioned configuration backup. `snapshot_version` is bumped on
   * breaking schema changes; the import handler rejects unknown versions.
   */
  'snapshot': ConfigSnapshot;
}
export type { ImportConfigPayload };