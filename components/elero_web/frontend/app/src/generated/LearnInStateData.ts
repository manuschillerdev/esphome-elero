import {LearnInState} from './LearnInState';
/**
 * Thin snapshot of the backend learn-in session.
 */
interface LearnInStateData {
  /**
   * Both direction buttons are available for the first movement.
   */
  'first_direction_pending'?: boolean;
  /**
   * The operator checked that the drive stopped and the new channel controls it.
   */
  'verified'?: boolean;
  /**
   * Backend learn-in session state. Complete means the hub sent the final confirmation. See verified for the operator check.
   */
  'state': LearnInState;
  /**
   * True while a learn-in session is logically in progress.
   */
  'active': boolean;
  /**
   * True while RF transmissions for the current learn-in step are pending.
   */
  'busy': boolean;
  /**
   * Virtual remote source address for the current session, if any.
   * @example 0x17a753
   */
  'src_address'?: string;
  /**
   * RF channel for the current session, if any.
   * @example 5
   */
  'channel'?: number;
}
export type { LearnInStateData };