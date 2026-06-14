
/**
 * Create or update a saved group in NVS. The backend validates that all
 * `device_ids` resolve to existing non-remote devices and that the
 * resolved devices are either all covers or all lights.
 */
interface UpsertGroupPayload {
  'type': 'upsert_group';
  /**
   * Stable group id.
   */
  'id': string;
  /**
   * Display name.
   */
  'name': string;
  'device_ids': string[];
}
export { UpsertGroupPayload };