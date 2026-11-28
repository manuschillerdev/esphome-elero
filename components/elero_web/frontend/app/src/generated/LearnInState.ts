
/**
 * Backend learn-in session state.
 */
type LearnInState = "idle" | "programming" | "wait_up" | "confirming_up" | "wait_down" | "confirming_down" | "complete" | "failed" | "cancelled" | "timed_out" | "opening_standby" | "standby_sent" | "train_p1" | "train_p2" | "train_p3" | "watch_movement" | "train_up" | "train_down" | "acknowledging" | "ack_sent" | "removing_link" | "removal_sent";
export type { LearnInState };