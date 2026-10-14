import {ErrorData} from './ErrorData';
interface ErrorEventEnvelope {
  'event': 'error';
  'data': ErrorData;
}
export type { ErrorEventEnvelope };