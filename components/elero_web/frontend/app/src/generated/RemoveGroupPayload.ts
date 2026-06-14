
/**
 * Remove a saved group from NVS.
 */
interface RemoveGroupPayload {
  'type': 'remove_group';
  /**
   * Group id.
   */
  'id': string;
}
export { RemoveGroupPayload };