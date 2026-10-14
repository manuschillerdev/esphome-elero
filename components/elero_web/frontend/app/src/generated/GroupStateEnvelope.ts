import {GroupStateData} from './GroupStateData';
interface GroupStateEnvelope {
  'event': 'group_state_changed';
  'data': GroupStateData;
  'additionalProperties'?: Map<string, any>;
}
export type { GroupStateEnvelope };