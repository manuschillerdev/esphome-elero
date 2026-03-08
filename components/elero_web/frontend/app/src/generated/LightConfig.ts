
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
   * Whether the device is published to Home Assistant
   */
  'enabled': boolean;
  /**
   * Dim duration in milliseconds (0 = on/off only)
   * @example 5000
   */
  'dim_ms': number;
  /**
   * Timestamp (millis()) when last persisted to NVS (0 = YAML-defined)
   * @example 1234567
   */
  'updated_at'?: number;
}
export { LightConfig };