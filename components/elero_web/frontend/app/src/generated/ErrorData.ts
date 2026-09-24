import {OperationFailureStatus} from './OperationFailureStatus';
interface ErrorData {
  'operation'?: string;
  'operation_id'?: number;
  'status'?: OperationFailureStatus;
  /**
   * Human-readable error message
   * @example Missing dst_address, CRUD not supported in native mode
   */
  'msg': string;
}
export { ErrorData };