
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
   * RF channel
   * @example 5
   */
  'channel': number;
  /**
   * Message type byte (hex string):
   * - `0x44` — button press/release (broadcast)
   * - `0x6a` — targeted command to blind
   * - `0x69` — alternate command format
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
   * Command byte (hex string, for command packets):
   * - `0x00` — check (request status)
   * - `0x10` — stop
   * - `0x20` — up/open
   * - `0x24` — tilt
   * - `0x40` — down/close
   * - `0x44` — intermediate position
   * @example 0x20
   */
  'command': string;
  /**
   * State byte (hex string, for status packets):
   * - `0x00` — unknown
   * - `0x01` — top (fully open)
   * - `0x02` — bottom (fully closed)
   * - `0x03` — intermediate
   * - `0x04` — tilt
   * - `0x05` — blocking (obstacle)
   * - `0x06` — overheated
   * - `0x07` — timeout
   * - `0x08` — start moving up
   * - `0x09` — start moving down
   * - `0x0a` — moving up
   * - `0x0b` — moving down
   * - `0x0d` — stopped
   * - `0x0e` — top + tilted
   * - `0x0f` — bottom + tilted / light off
   * - `0x10` — light on
   * @example 0x01
   */
  'state': string;
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
export { RfData };