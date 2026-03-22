import {HubConfig} from './HubConfig';
import {RadioConfig} from './RadioConfig';
import {BlindConfig} from './BlindConfig';
import {LightConfig} from './LightConfig';
import {RemoteConfig} from './RemoteConfig';
interface ConfigData {
  /**
   * Gateway identity and operating mode
   */
  'hub': HubConfig;
  /**
   * RF radio hardware configuration and capabilities
   */
  'radio': RadioConfig;
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
}
export { ConfigData };