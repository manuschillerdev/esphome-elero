import {DeviceType} from './DeviceType';
import {AnonymousSchema_54} from './AnonymousSchema_54';
interface GroupStateData {
  'id': string;
  'available': boolean;
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
  'supports_position': boolean;
  'position': number;
  'operation': AnonymousSchema_54;
  'is_on': boolean;
}
export type { GroupStateData };