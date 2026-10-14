
interface ImportError {
  /**
   * 0-based index into `snapshot.devices` (-1 for envelope-level errors).
   */
  'index': number;
  /**
   * Human-readable error message.
   */
  'msg': string;
}
export type { ImportError };