import {ImportError} from './ImportError';
/**
 * Summary of one import operation.
 */
interface ImportResult {
  /**
   * Devices created (no prior `(type, dst_address)` match).
   */
  'added': number;
  /**
   * Devices updated in place.
   */
  'updated': number;
  /**
   * Devices not applied (errored or no free slot).
   */
  'skipped': number;
  /**
   * Groups created.
   */
  'groups_added': number;
  /**
   * Groups updated in place.
   */
  'groups_updated': number;
  /**
   * Groups not applied.
   */
  'groups_skipped': number;
  /**
   * Whether hub-level overrides were applied.
   */
  'hub_applied'?: boolean;
  /**
   * Per-entry errors. Empty on full success.
   */
  'errors': ImportError[];
}
export { ImportResult };