
/**
 * Backend learn-in session state. Complete means the hub sent the final confirmation. See verified for the operator check.
 */
type LearnInState = "idle" | "programming" | "wait_up" | "confirming_up" | "wait_down" | "confirming_down" | "complete" | "failed" | "cancelled" | "timed_out";
export type { LearnInState };