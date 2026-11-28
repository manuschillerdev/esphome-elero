import {LearnInMode} from './LearnInMode';
/**
 * Starts the selected learn-in operation. The default is power-cycle pairing.
 */
interface LearnInStartPayload {
  'mode'?: LearnInMode;
  'receiver_address'?: string;
  /**
   * Must be true to remove the gateway channel from every receiver that learned it.
   */
  'confirm_all_receivers'?: boolean;
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
export type { LearnInStartPayload };