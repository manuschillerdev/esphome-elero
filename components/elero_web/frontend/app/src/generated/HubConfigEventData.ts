
/**
 * Hub-level config snapshot after an update.
 */
interface HubConfigEventData {
  /**
   * Effective hub display name (override or YAML default).
   * @example Elero Gateway
   */
  'name': string;
}
export type { HubConfigEventData };