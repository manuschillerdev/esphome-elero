import {CrudEventType} from './CrudEventType';
import {CrudEventData} from './CrudEventData';
/**
 * Envelope for device_upserted and device_removed events.
 */
interface CrudEventEnvelope {
  /**
   * CRUD event type for device lifecycle notifications
   */
  'event': CrudEventType;
  /**
   * Payload for device_upserted and device_removed events.
   */
  'data': CrudEventData;
}
export { CrudEventEnvelope };