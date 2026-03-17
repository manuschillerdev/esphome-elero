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
  /**
   * Matter commissioning QR code payload (MT:... string). Only present in Matter mode.
   * @example MT:Y3.13OTB00KA0648G00
   */
  'qr_code'?: string;
  /**
   * Matter 11-digit manual pairing code. Only present in Matter mode.
   * @example 34970112332
   */
  'manual_code'?: string;
}
export { ConfigData };