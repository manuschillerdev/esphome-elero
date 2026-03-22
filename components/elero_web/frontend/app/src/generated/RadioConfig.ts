import {FreqConfig} from './FreqConfig';
/**
 * RF radio hardware configuration and capabilities
 */
interface RadioConfig {
  /**
   * Radio chip identifier
   * @example cc1101, sx1262
   */
  'chipset': string;
  /**
   * Receiver sensitivity in dBm (e.g., -104 for CC1101, -117 for SX1262). Used to derive signal strength thresholds.
   * @example -104, -117
   */
  'rx_sensitivity': number;
  /**
   * Frequency register values (chip-specific encoding)
   */
  'freq': FreqConfig;
}
export { RadioConfig };