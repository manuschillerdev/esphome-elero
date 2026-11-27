
/**
 * Receiver status name returned by elero_state_to_string().
 * The enum index is the numeric status byte.
 */
type RfStateName = "unknown" | "top" | "bottom" | "intermediate" | "tilt" | "blocking" | "overheated" | "timeout" | "start_moving_up" | "start_moving_down" | "moving_up" | "moving_down" | "delete_in_remote" | "stopped" | "top_tilt" | "bottom_tilt" | "light_off" | "light_on" | "scene_set" | "scene_started" | "scene_stopped" | "scene_error";
export type { RfStateName };