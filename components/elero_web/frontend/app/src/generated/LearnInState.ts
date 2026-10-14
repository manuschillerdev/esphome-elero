
/**
 * Backend learn-in session state.
 */
type LearnInState = "idle" | "programming" | "wait_up" | "confirming_up" | "wait_down" | "confirming_down" | "complete" | "failed" | "cancelled" | "timed_out";
export type { LearnInState };