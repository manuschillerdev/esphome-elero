import {GroupRemovedData} from './GroupRemovedData';
/**
 * Envelope for group_removed events.
 */
interface GroupRemovedEnvelope {
  'event': 'group_removed';
  'data': GroupRemovedData;
}
export { GroupRemovedEnvelope };