import {RfData} from './RfData';
interface RfEventEnvelope {
  'event': 'rf';
  'data': RfData;
}
export { RfEventEnvelope };