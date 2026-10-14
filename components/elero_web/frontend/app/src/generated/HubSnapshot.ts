
/**
 * Hub-level overrides to restore. All fields optional.
 */
interface HubSnapshot {
  /**
   * Persisted hub display name override. Empty string clears the
   * override and falls back to the YAML default.
   * @example Living Room Hub
   */
  'name_override'?: string;
}
export type { HubSnapshot };