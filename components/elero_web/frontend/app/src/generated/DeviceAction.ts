
/**
 * Action to perform on a device:
 * - Covers: up/open, down/close, stop, check, tilt, int (intermediate)
 * - Lights: on, off, stop, check, dim_up, dim_down
 */
type DeviceAction = "up" | "down" | "open" | "close" | "stop" | "check" | "tilt" | "on" | "off" | "dim_up" | "dim_down" | "int";
export { DeviceAction };