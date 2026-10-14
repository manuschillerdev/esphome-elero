
/**
 * Operating mode of the hub. Devices always live in NVS (RFC-002);
 * the mode only selects how they're surfaced to Home Assistant:
 *   - `native` — ESPHome native API (NvsAdapter creates entities at boot)
 *   - `mqtt`   — MQTT HA discovery (MqttAdapter publishes topics)
 */
type HubMode = "native" | "mqtt";
export type { HubMode };