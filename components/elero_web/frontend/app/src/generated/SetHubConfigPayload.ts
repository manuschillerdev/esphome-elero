
/**
 * Update hub-level settings. Currently only the display name override is
 * configurable. An empty `name` clears the override and falls back to the
 * YAML-configured default.
 */
interface SetHubConfigPayload {
  'type': 'set_hub_config';
  /**
   * New hub display name. Empty string clears the override.
   * @example Living Room Hub
   */
  'name': string;
}
export type { SetHubConfigPayload };