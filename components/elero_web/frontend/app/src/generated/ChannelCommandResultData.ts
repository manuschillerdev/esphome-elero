import {TransmissionStatus} from './TransmissionStatus';
/**
 * Broadcast RF progress, not a motor acknowledgement. STOP cancels the previous pending operation.
 */
interface ChannelCommandResultData {
  'operation_id': number;
  'remote': string;
  'channel': number;
  'command': number;
  'status': TransmissionStatus;
}
export { ChannelCommandResultData };