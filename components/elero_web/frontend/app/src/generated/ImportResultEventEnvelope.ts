import {ImportResult} from './ImportResult';
interface ImportResultEventEnvelope {
  'event': 'import_result';
  /**
   * Summary of one import operation.
   */
  'data': ImportResult;
}
export type { ImportResultEventEnvelope };