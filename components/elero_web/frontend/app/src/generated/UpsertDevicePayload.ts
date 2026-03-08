import {DeviceType} from './DeviceType';
/**
 * Create or update a device in NVS. The combination of `dst_address` + `device_type`
 * identifies the device. All other fields are updateable.
 * Only available when `crud` is `true` in the config event.
 */
interface UpsertDevicePayload {
  'type': 'upsert_device';
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
  /**
   * 3-byte destination address (hex string)
   * @example 0xa831e5
   */
  'dst_address': string;
  /**
   * Display name for the device
   * @example Living Room Blind
   */
  'name'?: string;
  /**
   * Whether the device is enabled (default true)
   */
  'enabled'?: boolean;
  /**
   * 3-byte source/remote address (hex string). Required for cover and light, ignored for remote.
   * @example 0xb42f01
   */
  'src_address'?: string;
  /**
   * RF channel number. Required for cover and light, ignored for remote.
   * @example 5
   */
  'channel'?: number;
  /**
   * Hop count (hex string, default 0x0a). Ignored for remote.
   * @example 0x0a
   */
  'hop'?: string;
  /**
   * Payload byte 1 (hex string, default 0x00). Ignored for remote.
   * @example 0x00
   */
  'payload_1'?: string;
  /**
   * Payload byte 2 (hex string, default 0x04). Ignored for remote.
   * @example 0x04
   */
  'payload_2'?: string;
  /**
   * Message type byte (hex string, default 0x6a). Ignored for remote.
   * @example 0x6a
   */
  'msg_type'?: string;
  /**
   * Secondary type byte (hex string, default 0x00). Ignored for remote.
   * @example 0x00
   */
  'type2'?: string;
  /**
   * Full open duration in milliseconds (cover only, 0 = position tracking disabled)
   * @example 25000
   */
  'open_duration_ms'?: number;
  /**
   * Full close duration in milliseconds (cover only, 0 = position tracking disabled)
   * @example 23000
   */
  'close_duration_ms'?: number;
  /**
   * Status poll interval in milliseconds (cover only)
   * @example 300000
   */
  'poll_interval_ms'?: number;
  /**
   * Dim duration in milliseconds (light only, 0 = on/off only)
   * @example 5000
   */
  'dim_duration_ms'?: number;
  /**
   * Whether the cover supports tilt (cover only, default false)
   */
  'supports_tilt'?: boolean;
}
export { UpsertDevicePayload };