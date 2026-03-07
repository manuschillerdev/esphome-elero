
interface RawPayload {
  'type': 'raw';
  /**
   * 3-byte destination address (hex string)
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
   * Payload byte 2 (hex string, default 0x04)
   * @example 0x04
   */
  'payload_2'?: string;
  /**
   * Message type byte (hex string, default 0x6a)
   * @example 0x6a
   */
  'msg_type'?: string;
  /**
   * Secondary type byte (hex string, default 0x00)
   * @example 0x00
   */
  'type2'?: string;
  /**
   * Hop count byte (hex string, default 0x0a)
   * @example 0x0a
   */
  'hop'?: string;
}
export { RawPayload };