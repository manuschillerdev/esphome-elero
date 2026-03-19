
/**
 * RF state name as returned by elero_state_to_string().
 * Maps 1:1 to the protocol state bytes:
 *   0x00=unknown, 0x01=top, 0x02=bottom, 0x03=intermediate,
 *   0x04=tilt, 0x05=blocking, 0x06=overheated, 0x07=timeout,
 *   0x08=start_moving_up, 0x09=start_moving_down, 0x0a=moving_up,
 *   0x0b=moving_down, 0x0d=stopped, 0x0e=top_tilt,
 *   0x0f=bottom_tilt, 0x10=light_on
 */
type RfStateName = "unknown" | "top" | "bottom" | "intermediate" | "tilt" | "blocking" | "overheated" | "timeout" | "start_moving_up" | "start_moving_down" | "moving_up" | "moving_down" | "stopped" | "top_tilt" | "bottom_tilt" | "light_on";
export { RfStateName };