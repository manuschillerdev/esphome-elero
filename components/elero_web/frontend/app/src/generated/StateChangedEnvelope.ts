import {StateChangedData} from './StateChangedData';
interface StateChangedEnvelope {
  'event': 'state_changed';
  /**
   * Snapshot of a device's derived state at the moment of change.
   * Sent on user commands (optimistic), RF status responses (confirmed),
   * FSM timeouts (recovery), and throttled position updates during movement.
   * Fields vary by device_type — covers include position/ha_state/tilted/device_class,
   * lights include is_on/brightness.
   */
  'data': StateChangedData;
}
export { StateChangedEnvelope };