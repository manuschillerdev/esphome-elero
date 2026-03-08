import {DeviceType} from './DeviceType';
/**
 * Full device config returned by the server after a successful upsert.
 * Contains all fields needed to construct a client-side Device entry.
 * Fields vary by device_type: covers include open_ms/close_ms/poll_ms/supports_tilt,
 * lights include dim_ms, remotes have only address/device_type/name.
 */
interface DeviceUpsertedData {
  /**
   * 3-byte device address (hex string)
   * @example 0xa831e5
   */
  'address': string;
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
  /**
   * Display name
   * @example Living Room Blind
   */
  'name'?: string;
  /**
   * RF channel number (covers and lights only)
   * @example 5
   */
  'channel'?: number;
  /**
   * 3-byte remote address (hex string, covers and lights only)
   * @example 0xb42f01
   */
  'remote'?: string;
  /**
   * Whether the device is published to Home Assistant
   */
  'enabled'?: boolean;
  /**
   * Full open duration in ms (covers only)
   * @example 25000
   */
  'open_ms'?: number;
  /**
   * Full close duration in ms (covers only)
   * @example 23000
   */
  'close_ms'?: number;
  /**
   * Status poll interval in ms (covers only)
   * @example 300000
   */
  'poll_ms'?: number;
  /**
   * Whether the cover supports tilt (covers only)
   */
  'supports_tilt'?: boolean;
  /**
   * Dim duration in ms (lights only, 0 = on/off only)
   * @example 5000
   */
  'dim_ms'?: number;
  /**
   * Timestamp (millis()) when the device was last persisted to NVS. Non-zero means the device is server-confirmed.
   * @example 1234567
   */
  'updated_at'?: number;
}
export { DeviceUpsertedData };