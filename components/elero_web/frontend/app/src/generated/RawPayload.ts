
interface RawPayload {
  'type': 'raw';
  /**
   * Receiver address, or zero or the channel selector for type 0x44
   * @example 0xa831e5
   */
  'dst_address': string;
  /**
   * 3-byte source address (hex string)
   * @example 0xb42f01
   */
  'src_address': string;
  /**
   * RF channel (default 0)
   * @example 5
   */
  'channel'?: number;
  /**
   * Command byte (hex string, default 0x00)
   * @example 0x20
   */
  'command'?: string;
  /**
   * Payload byte 1 (hex string, default 0x00)
   * @example 0x00
   */
  'payload_1'?: string;
  /**
   * Payload byte 2 (0x03 for a button, otherwise 0x04)
   * @example 0x04
   */
  'payload_2'?: string;
  /**
   * Message type byte (0x44, 0x69, or the default 0x6a)
   * @example 0x6a
   */
  'msg_type'?: string;
  /**
   * Secondary type byte (default 0x10 for a button, otherwise 0x00)
   * @example 0x00
   */
  'type2'?: string;
  /**
   * Hop count byte (default 0x00 for a button, otherwise 0x0a)
   * @example 0x0a
   */
  'hop'?: string;
}
export type { RawPayload };