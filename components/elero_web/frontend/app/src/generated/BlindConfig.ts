
interface BlindConfig {
  /**
   * 3-byte destination address (hex string)
   * @example 0xa831e5
   */
  'address': string;
  /**
   * Entity name
   * @example Living Room Blind
   */
  'name': string;
  /**
   * RF channel number
   * @example 5
   */
  'channel': number;
  /**
   * 3-byte source/remote address (hex string)
   * @example 0xb42f01
   */
  'remote': string;
  /**
   * Full open duration in milliseconds (0 = position tracking disabled)
   * @example 25000
   */
  'open_ms': number;
  /**
   * Full close duration in milliseconds (0 = position tracking disabled)
   * @example 23000
   */
  'close_ms': number;
  /**
   * Status poll interval in milliseconds
   * @example 300000
   */
  'poll_ms': number;
  /**
   * Whether the blind supports tilt
   */
  'tilt': boolean;
}
export { BlindConfig };