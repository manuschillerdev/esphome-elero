import {FreqConfig} from './FreqConfig';
import {BlindConfig} from './BlindConfig';
import {LightConfig} from './LightConfig';
import {RemoteConfig} from './RemoteConfig';
import {HubMode} from './HubMode';
interface ConfigData {
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
   * CC1101 frequency register values
   */
  'freq': FreqConfig;
  /**
   * Configured cover entities
   */
  'blinds': BlindConfig[];
  /**
   * Configured light entities
   */
  'lights': LightConfig[];
  /**
   * Known remote controls. In native mode, derived from cover/light src_address. In NVS modes, includes auto-discovered remotes.
   */
  'remotes': RemoteConfig[];
  /**
   * Operating mode of the hub
   */
  'mode': HubMode;
  /**
   * Whether CRUD operations (upsert_device, remove_device) are supported
   */
  'crud': boolean;
}
export { ConfigData };