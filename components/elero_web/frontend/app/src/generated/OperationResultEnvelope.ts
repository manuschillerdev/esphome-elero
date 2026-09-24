import {OperationResultData} from './OperationResultData';
interface OperationResultEnvelope {
  'event': 'operation_result';
  /**
   * Applied means flash sync succeeded. Queued does not mean transmitted or motor acknowledged.
   */
  'data': OperationResultData;
}
export { OperationResultEnvelope };