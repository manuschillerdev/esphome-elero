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
  'dst'?: string;
  /**
   * False for a complete frame with an unsupported layout. Only the common header, signal strength and raw bytes are available.
   */
  'decoded'?: boolean;
  /**
   * Channel selectors in received order. Empty for addressed packets.
   */
  'selectors'?: number[];
  /**
   * The two bytes before the command block, in hexadecimal.
   */
  'prefix'?: string;
  /**
   * The eight decoded command or status bytes, in hexadecimal.
   */
  'decoded_bytes'?: string;
  /**
   * Bytes after the command block, excluding radio status bytes, in hexadecimal.
   */
  'trailing'?: string;
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
   * Command byte (hex string, for command/selector packets):
   * - `0x00` — release in a 0x44 button packet, check in a 0x6a command packet
   * - `0x10` — stop
   * - `0x20` — up/open
   * - `0x24` — tilt
   * - `0x40` — down/close
   * - `0x44` — intermediate position
   * - `0x80` — programming/P selector action
   * - `0x81` — programming/P group selector action
   * - `0x84` — programming/P targeted action
   * @example 0x20
   */
  'command'?: string;
  /**
   * Raw hexadecimal status byte. For a status packet, state_name gives the firmware's decoded name.
   * @example 0x01
   */
  'state'?: string;
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