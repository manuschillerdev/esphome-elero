
/**
 * Saved group metadata. Groups are not devices and do not store a device
 * type; the backend derives and validates the common member type from the
 * referenced device ids. Device ids are stable 3-byte device addresses.
 */
interface GroupConfig {
  /**
   * Stable group id persisted in NVS.
   * @example grp_living_k8x2p1
   */
  'id': string;
  /**
   * Display name.
   * @example Living Room
   */
  'name': string;
  /**
   * Stable device address ids. All must resolve to existing covers or all to existing lights.
   */
  'device_ids': string[];
}
export { GroupConfig };