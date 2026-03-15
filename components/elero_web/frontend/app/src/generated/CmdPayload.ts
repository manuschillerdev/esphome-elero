import {DeviceAction} from './DeviceAction';
interface CmdPayload {
  'type': 'cmd';
  /**
   * 3-byte destination address of the target device (hex string)
   * @example 0xa831e5
   */
  'address': string;
  /**
   * Action to perform on a device:
   * - Covers: up/open, down/close, stop, check, tilt, int (intermediate)
   * - Lights: on, off, stop, check, dim_up, dim_down
   */
  'action': DeviceAction;
}
export { CmdPayload };