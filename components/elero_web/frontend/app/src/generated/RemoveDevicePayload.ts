import {DeviceType} from './DeviceType';
/**
 * Remove a device from NVS by address and type.
 * Only available when `crud` is `true` in the config event.
 */
interface RemoveDevicePayload {
  'type': 'remove_device';
  /**
   * 3-byte destination address (hex string)
   * @example 0xa831e5
   */
  'dst_address': string;
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
}
export { RemoveDevicePayload };