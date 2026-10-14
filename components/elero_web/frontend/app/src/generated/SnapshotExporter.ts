
/**
 * Identification of the device that produced the snapshot.
 */
interface SnapshotExporter {
  /**
   * Source device name (`esphome.name`).
   * @example elero-gateway
   */
  'device': string;
  /**
   * Source component version string.
   * @example 0.10.0
   */
  'version': string;
}
export type { SnapshotExporter };