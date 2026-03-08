import {CrudEventData} from './CrudEventData';
/**
 * Envelope for device_removed events.
 */
interface CrudEventEnvelope {
  'event': 'device_removed';
  /**
   * Payload for device_removed events.
   */
  'data': CrudEventData;
}
export { CrudEventEnvelope };