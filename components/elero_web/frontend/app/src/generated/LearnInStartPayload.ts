
/**
 * Starts a backend learn-in session. The frontend must already have guided
 * the user through the required power-cycle of the target motor.
 */
interface LearnInStartPayload {
  'type': 'learn_in_start';
  /**
   * 3-byte virtual remote/source address (hex string)
   * @example 0x17a753
   */
  'src_address': string;
  /**
   * RF channel to learn in
   * @example 5
   */
  'channel': number;
  /**
   * Learn-in session timeout in milliseconds (default 300000)
   * @example 300000
   */
  'session_timeout_ms'?: number;
}
export { LearnInStartPayload };