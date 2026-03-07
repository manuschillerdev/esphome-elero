import {DeviceType} from './DeviceType';
/**
 * Payload for device_upserted and device_removed events.
 */
interface CrudEventData {
  /**
   * 3-byte device address (hex string)
   * @example 0xa831e5
   */
  'address': string;
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
}
export { CrudEventData };