import {HubMode} from './HubMode';
/**
 * Gateway identity and operating mode
 */
interface HubConfig {
  /**
   * ESPHome device name (from `esphome.name` in YAML)
   * @example elero-gateway
   */
  'device': string;
  /**
   * Component version string
   * @example 1.2.3
   */
  'version': string;
  /**
   * Operating mode of the hub
   */
  'mode': HubMode;
  /**
   * Whether CRUD operations (upsert_device, remove_device) are supported
   */
  'crud': boolean;
}
export { HubConfig };