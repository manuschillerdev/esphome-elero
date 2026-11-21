
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
   * False for a complete frame with an unsupported layout.
   */
  'decoded'?: boolean;
  /**
   * Ordered one-byte channel selectors.
   */
  'selectors'?: string[];
  'prefix'?: string[];
  'decoded_bytes'?: string[];
  /**
   * Radio link quality, from 0 to 127.
   */
  'lqi'?: number;
  /**
   * The radio checksum passed.
   */
  'crc_ok'?: boolean;
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
   * - `0x00` — button release on 0x44, status request on 0x6a
   * - `0x10` — STOP key (the physical gesture on 0x45 is unknown)
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
   * State byte (hex string, for status packets):
   * - `0x00` — unknown
   * - `0x01` — top (fully open)
   * - `0x02` — bottom (fully closed)
   * - `0x03` — intermediate
   * - `0x04` — tilt
   * - `0x05` — blocking (obstacle)
   * - `0x06` — overheated
   * - `0x07` — receiver timeout or end of run time
   * - `0x08` — start moving up
   * - `0x09` — start moving down
   * - `0x0a` — moving up
   * - `0x0b` — moving down
   * - `0x0c` — delete in remote
   * - `0x0d` — stopped
   * - `0x0e` — top + tilted
   * - `0x0f` — bottom + stored intermediate position
   * - `0x10` — light off
   * - `0x11` — light on
   * - `0x12` — scene set
   * - `0x13` — scene started
   * - `0x14` — scene stopped
   * - `0x15` — scene error
   * @example 0x01
   */
  'state'?: string;
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
   * Length byte and packet body in hex. Radio metadata is separate.
   * @example 1d 2a 6a 00 0a 01 05 a8 31 e5 ...
   */
  'raw': string;
}
export type { RfData };