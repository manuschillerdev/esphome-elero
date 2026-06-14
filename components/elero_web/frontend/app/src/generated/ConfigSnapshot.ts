import {SnapshotExporter} from './SnapshotExporter';
import {HubSnapshot} from './HubSnapshot';
import {DeviceSnapshot} from './DeviceSnapshot';
import {GroupConfig} from './GroupConfig';
/**
 * Versioned configuration backup. `snapshot_version` is bumped on
 * breaking schema changes; the import handler rejects unknown versions.
 */
interface ConfigSnapshot {
  /**
   * Snapshot envelope version (currently 2). Must match the C++
   * `SNAPSHOT_VERSION` const in `components/elero_web/elero_web_server.cpp` —
   * the import handler rejects newer values it doesn't recognise.
   * @example 2
   */
  'snapshot_version': number;
  /**
   * Timestamp (millis() since boot on the source device) when exported.
   * @example 1715000000000
   */
  'exported_at': number;
  /**
   * Identification of the device that produced the snapshot.
   */
  'exporter': SnapshotExporter;
  /**
   * Hub-level overrides to restore. All fields optional.
   */
  'hub': HubSnapshot;
  /**
   * All active devices, in slot order.
   */
  'devices': DeviceSnapshot[];
  /**
   * Saved groups.
   */
  'groups': GroupConfig[];
}
export { ConfigSnapshot };