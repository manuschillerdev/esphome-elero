import {LogData} from './LogData';
interface LogEventEnvelope {
  'event': 'log';
  /**
   * Elero-related ESPHome logs, with ANSI escapes removed.
   */
  'data': LogData;
}
export type { LogEventEnvelope };