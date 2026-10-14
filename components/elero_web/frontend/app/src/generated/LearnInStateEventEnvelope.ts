import {LearnInStateData} from './LearnInStateData';
interface LearnInStateEventEnvelope {
  'event': 'learn_in_state';
  /**
   * Thin snapshot of the backend learn-in session.
   */
  'data': LearnInStateData;
}
export type { LearnInStateEventEnvelope };