import {HubConfigEventData} from './HubConfigEventData';
interface HubConfigEventEnvelope {
  'event': 'hub_config';
  /**
   * Hub-level config snapshot after an update.
   */
  'data': HubConfigEventData;
}
export type { HubConfigEventEnvelope };