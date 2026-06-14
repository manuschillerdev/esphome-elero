import {GroupConfig} from './GroupConfig';
/**
 * Envelope for group_upserted events.
 */
interface GroupUpsertedEnvelope {
  'event': 'group_upserted';
  /**
   * Saved group metadata. Groups are not devices and do not store a device
   * type; the backend derives and validates the common member type from the
   * referenced device ids. Device ids are stable 3-byte device addresses.
   */
  'data': GroupConfig;
}
export { GroupUpsertedEnvelope };