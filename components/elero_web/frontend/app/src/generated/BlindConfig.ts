
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
   * Whether the device is published to Home Assistant
   */
  'enabled': boolean;
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
  'supports_tilt': boolean;
  /**
   * Timestamp (millis()) when last persisted to NVS (0 = YAML-defined)
   * @example 1234567
   */
  'updated_at'?: number;
  /**
   * Last known cover position (0.0 = closed, 1.0 = open)
   */
  'position'?: number;
  /**
   * Last known state byte (hex string, e.g. "0x01" = top)
   * @example 0x0d
   */
  'state'?: string;
  /**
   * Last known RSSI in dBm
   * @example -65.5
   */
  'rssi'?: number;
  /**
   * Timestamp (millis()) of last RF packet from this blind
   * @example 408
   */
  'last_seen'?: number;
}
export { BlindConfig };