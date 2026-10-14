import {DeviceType} from './DeviceType';
/**
 * One device. Mirrors `NvsDeviceConfig` fields. Address fields are
 * hex strings (e.g., "0xa831e5"); the import handler accepts both
 * hex strings and decimal integers for backward compatibility.
 */
interface DeviceSnapshot {
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
  /**
   * @example 0xa831e5
   */
  'dst_address': string;
  /**
   * Source/remote address (covers and lights only).
   * @example 0xb42f01
   */
  'src_address'?: string;
  /**
   * RF channel (covers and lights only).
   * @example 5
   */
  'channel'?: number;
  /**
   * @example Living Room Blind
   */
  'name'?: string;
  'enabled'?: boolean;
  /**
   * Cover open duration in ms (0 = position tracking disabled).
   * @example 25000
   */
  'open_duration_ms'?: number;
  /**
   * Cover close duration in ms (0 = position tracking disabled).
   * @example 22000
   */
  'close_duration_ms'?: number;
  /**
   * Light dim duration in ms (0 = on/off only).
   * @example 5000
   */
  'dim_duration_ms'?: number;
  /**
   * Cover supports tilt.
   */
  'supports_tilt'?: boolean;
  /**
   * HaCoverClass enum value (cover only). 0 = shutter.
   * @example 0
   */
  'ha_device_class'?: number;
  /**
   * Hop count byte (hex string, default "0x0a").
   * @example 0x0a
   */
  'hop'?: string;
  /**
   * Payload byte 1 (hex string, default "0x00").
   * @example 0x00
   */
  'payload_1'?: string;
  /**
   * Payload byte 2 (hex string, default "0x04").
   * @example 0x04
   */
  'payload_2'?: string;
  /**
   * Message type byte (hex string, default "0x6a").
   * @example 0x6a
   */
  'msg_type'?: string;
  /**
   * Secondary type byte (hex string, default "0x00").
   * @example 0x00
   */
  'type2'?: string;
  /**
   * Per-cover RF command byte; defaults to 0x20.
   */
  'command_up'?: string;
  /**
   * Per-cover RF command byte; defaults to 0x40.
   */
  'command_down'?: string;
  /**
   * Per-cover RF command byte; defaults to 0x10.
   */
  'command_stop'?: string;
  /**
   * Per-cover RF command byte; defaults to 0x00.
   */
  'command_check'?: string;
  /**
   * Per-cover RF command byte; defaults to 0x24.
   */
  'command_tilt'?: string;
}
export type { DeviceSnapshot };