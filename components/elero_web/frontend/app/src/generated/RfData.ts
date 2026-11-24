import {RfCommandName} from './RfCommandName';
import {RfStateName} from './RfStateName';
interface RfData {
  /**
   * Timestamp in milliseconds since boot (millis())
   * @example 1234567
   */
  't': number;
  /**
   * Source address (hex string). Remote for commands, blind for status.
   * @example 0xa831e5
   */
  'src': string;
  /**
   * Destination address (hex string). Blind for commands, remote for status.
   * @example 0xb42f01
   */
  'dst': string;
  /**
   * All 24-bit destinations for addressed packets. Empty for selector envelopes.
   */
  'destinations'?: string[];
  /**
   * RF channel
   * @example 5
   */
  'channel': number;
  /**
   * Message type byte (hex string):
   * - `0x44` — button press/release selector packet
   * - `0x45` — group/central selector packet
   * - `0x6a` — targeted command to blind
   * - `0x69` — alternate command format
   * - `0x70` — single-channel programming/P selector packet
   * - `0xca` — status response from blind
   * - `0xc9` — alternate status format
   * @example 0xca
   */
  'type': string;
  /**
   * Secondary type byte (hex string)
   * @example 0x00
   */
  'type2': string;
  /**
   * Raw command key. Its meaning depends on the frame type.
   * @example 0x20
   */
  'command': string;
  /**
   * Command meaning decoded by the hub from the frame type and key.
   */
  'command_name'?: RfCommandName;
  /**
   * Raw hexadecimal status byte. For a status packet, state_name gives the firmware's decoded name.
   * @example 0x01
   */
  'state': string;
  /**
   * Receiver status name returned by elero_state_to_string().
   * The enum index is the numeric status byte.
   */
  'state_name'?: RfStateName;
  /**
   * Rolling counter value from packet (0-255)
   * @example 42
   */
  'cnt': number;
  /**
   * Received signal strength in dBm
   * @example -65.5
   */
  'rssi': number;
  /**
   * Hop count byte (hex string)
   * @example 0x0a
   */
  'hop': string;
  /**
   * Space-separated hex bytes of the raw packet
   * @example 1d 2a 6a 00 0a 01 05 a8 31 e5 ...
   */
  'raw': string;
}
export type { RfData };