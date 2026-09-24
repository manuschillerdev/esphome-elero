import {ChannelCommandResultData} from './ChannelCommandResultData';
interface ChannelCommandResultEnvelope {
  'event': 'channel_command_result';
  /**
   * Broadcast RF progress, not a motor acknowledgement. STOP cancels the previous pending operation.
   */
  'data': ChannelCommandResultData;
}
export { ChannelCommandResultEnvelope };