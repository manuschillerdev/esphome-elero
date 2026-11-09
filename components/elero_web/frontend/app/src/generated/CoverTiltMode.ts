
/**
 * HA tilt requests recall the saved preset by default. Step mode selects one short DOWN below 50 percent or one short UP at or above 50 percent; no absolute angle is set.
 */
type CoverTiltMode = "preset" | "step";
export type { CoverTiltMode };