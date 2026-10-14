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
   * Operating mode of the hub. Devices always live in NVS (RFC-002);
   * the mode only selects how they're surfaced to Home Assistant:
   *   - `native` — ESPHome native API (NvsAdapter creates entities at boot)
   *   - `mqtt`   — MQTT HA discovery (MqttAdapter publishes topics)
   */
  'mode': HubMode;
  /**
   * Whether CRUD operations (upsert_device, remove_device) are supported
   */
  'crud': boolean;
  /**
   * Hub display name shown in Home Assistant (gateway device block) and in the web UI.
   * Defaults to the YAML-configured value (`elero_mqtt.device_name`); can be overridden
   * at runtime and persisted to NVS via `set_hub_config`.
   * @example Elero Gateway, Living Room Hub
   */
  'name': string;
  /**
   * Deterministic default 3-byte virtual remote/source address derived from the hub's
   * hardware MAC address. Used as the default learn-in remote identity unless the user
   * overrides it or restores a backup onto different hardware.
   * @example 0xb42f01
   */
  'default_src_address': string;
}
export type { HubConfig };