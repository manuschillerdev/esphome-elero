import {ConfigSnapshot} from './ConfigSnapshot';
interface ConfigSnapshotEventEnvelope {
  'event': 'config_snapshot';
  /**
   * Versioned configuration backup. `snapshot_version` is bumped on
   * breaking schema changes; the import handler rejects unknown versions.
   */
  'data': ConfigSnapshot;
}
export type { ConfigSnapshotEventEnvelope };