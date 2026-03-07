import {ConfigData} from './ConfigData';
interface ConfigEventEnvelope {
  'event': 'config';
  'data': ConfigData;
}
export { ConfigEventEnvelope };