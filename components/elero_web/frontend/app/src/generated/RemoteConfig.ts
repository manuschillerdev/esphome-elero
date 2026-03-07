
interface RemoteConfig {
  /**
   * 3-byte remote control address (hex string)
   * @example 0xb42f01
   */
  'address': string;
  /**
   * Display name. In native mode, defaults to the hex address.
   * @example Remote 0xb42f01
   */
  'name': string;
}
export { RemoteConfig };