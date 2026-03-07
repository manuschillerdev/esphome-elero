
interface LightConfig {
  /**
   * 3-byte destination address (hex string)
   * @example 0xc41a2b
   */
  'address': string;
  /**
   * Entity name
   * @example Patio Light
   */
  'name': string;
  /**
   * RF channel number
   * @example 3
   */
  'channel': number;
  /**
   * 3-byte source/remote address (hex string)
   * @example 0xd51e03
   */
  'remote': string;
  /**
   * Dim duration in milliseconds (0 = on/off only)
   * @example 5000
   */
  'dim_ms': number;
}
export { LightConfig };