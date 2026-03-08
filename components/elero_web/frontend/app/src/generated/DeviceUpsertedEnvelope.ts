import {DeviceUpsertedData} from './DeviceUpsertedData';
/**
 * Envelope for device_upserted events with full device config.
 */
interface DeviceUpsertedEnvelope {
  'event': 'device_upserted';
  /**
   * Full device config returned by the server after a successful upsert.
   * Contains all fields needed to construct a client-side Device entry.
   * Fields vary by device_type: covers include open_ms/close_ms/poll_ms/supports_tilt,
   * lights include dim_ms, remotes have only address/device_type/name.
   */
  'data': DeviceUpsertedData;
}
export { DeviceUpsertedEnvelope };