
/**
 * Elero-related ESPHome logs, with ANSI escapes removed.
 */
interface LogData {
  /**
   * Milliseconds since gateway boot.
   */
  't': number;
  /**
   * ESPHome numeric log level.
   */
  'level': number;
  /**
   * Component tag beginning with elero.
   */
  'tag': string;
  /**
   * Log message without ANSI escape sequences.
   */
  'msg': string;
}
export type { LogData };