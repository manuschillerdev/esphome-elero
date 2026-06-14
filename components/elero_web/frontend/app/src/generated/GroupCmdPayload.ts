import {DeviceAction} from './DeviceAction';
/**
 * Send an action to a saved group. Backend derives the common member type,
 * partitions members by remote/src address, and emits one RF group command
 * per remote bucket.
 */
interface GroupCmdPayload {
  'type': 'group_cmd';
  /**
   * Group id.
   */
  'id': string;
  /**
   * Action to perform on a device:
   * - Covers: up/open, down/close, stop, check, tilt, int (intermediate)
   * - Lights: on, off, stop, check, dim_up, dim_down
   */
  'action': DeviceAction;
}
export { GroupCmdPayload };