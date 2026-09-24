import {ChannelAction} from './ChannelAction';
interface ChannelCmdPayload {
  'type': 'channel_cmd';
  /**
   * Nonzero 24-bit remote address, prefixed with 0x for hexadecimal.
   */
  'src_address': string;
  'channel': number;
  'action': ChannelAction;
}
export { ChannelCmdPayload };