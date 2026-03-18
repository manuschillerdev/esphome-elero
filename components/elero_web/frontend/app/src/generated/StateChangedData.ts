import {DeviceType} from './DeviceType';
import {AnonymousSchema_71} from './AnonymousSchema_71';
import {AnonymousSchema_77} from './AnonymousSchema_77';
import {AnonymousSchema_80} from './AnonymousSchema_80';
/**
 * Snapshot of a device's derived state at the moment of change.
 * Sent on user commands (optimistic), RF status responses (confirmed),
 * FSM timeouts (recovery), and throttled position updates during movement.
 * Fields vary by device_type — covers include position/ha_state/tilted/device_class,
 * lights include is_on/brightness.
 */
interface StateChangedData {
  /**
   * 3-byte device address (hex string)
   * @example 0xa831e5
   */
  'address': string;
  /**
   * Type of device (cover, light, or remote control)
   */
  'device_type': DeviceType;
  /**
   * Cover position (0.0 = closed, 1.0 = open). Cover only.
   */
  'position'?: number;
  /**
   * HA-compatible cover state. Cover only.
   */
  'ha_state'?: AnonymousSchema_71;
  /**
   * Whether the cover is tilted. Cover only.
   */
  'tilted'?: boolean;
  /**
   * HA device class (shutter, blind, awning, etc.). Cover only.
   * @example shutter
   */
  'device_class'?: string;
  /**
   * Whether the light is on. Light only.
   */
  'is_on'?: boolean;
  /**
   * Light brightness (0.0–1.0). Light only.
   */
  'brightness'?: number;
  /**
   * Whether the device is in a problem state (blocking, overheated, timeout)
   */
  'is_problem'?: boolean;
  /**
   * Problem classification or "none"
   */
  'problem_type'?: AnonymousSchema_77;
  /**
   * Last RSSI in dBm
   * @example -65.5
   */
  'rssi'?: number;
  /**
   * Raw RF state name (top, moving_up, bottom_tilt, etc.)
   * @example moving_up
   */
  'state'?: string;
  /**
   * Who issued the last command
   */
  'command_source'?: AnonymousSchema_80;
  /**
   * Timestamp (millis()) of last RF packet from this device
   * @example 1234567
   */
  'last_seen'?: number;
}
export { StateChangedData };