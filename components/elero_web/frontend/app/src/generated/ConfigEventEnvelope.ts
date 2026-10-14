import {ConfigData} from './ConfigData';
interface ConfigEventEnvelope {
  'event': 'config';
  'data': ConfigData;
}
export type { ConfigEventEnvelope };