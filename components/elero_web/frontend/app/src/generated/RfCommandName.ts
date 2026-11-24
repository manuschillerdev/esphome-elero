
/**
 * Command meaning decoded by the hub from the frame type and key.
 */
type RfCommandName = "check" | "release" | "stop" | "up" | "full_up" | "full_down" | "tilt" | "down" | "intermediate" | "program" | "program_group" | "program_target";
export type { RfCommandName };