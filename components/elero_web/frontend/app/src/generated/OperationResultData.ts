import {OperationSuccessStatus} from './OperationSuccessStatus';
/**
 * Applied means flash sync succeeded. Queued does not mean transmitted or motor acknowledged.
 */
interface OperationResultData {
  'operation': string;
  /**
   * Nonzero for channel commands; hub-local until reboot. Zero for untracked operations.
   */
  'operation_id': number;
  'status': OperationSuccessStatus;
  'msg': string;
}
export { OperationResultData };