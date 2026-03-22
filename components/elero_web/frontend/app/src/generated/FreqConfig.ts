
/**
 * Frequency register values (chip-specific encoding)
 */
interface FreqConfig {
  /**
   * FREQ2 register (hex string)
   * @example 0x21
   */
  'freq2': string;
  /**
   * FREQ1 register (hex string)
   * @example 0x71
   */
  'freq1': string;
  /**
   * FREQ0 register (hex string). 0x7a = 868.35 MHz, 0xc0 = 868.95 MHz.
   * @example 0x7a
   */
  'freq0': string;
}
export { FreqConfig };