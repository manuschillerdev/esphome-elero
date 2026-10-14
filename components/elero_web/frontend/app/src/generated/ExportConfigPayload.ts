
/**
 * Trigger a backup snapshot. The server replies (to the requesting client only) with a `config_snapshot` event.
 */
interface ExportConfigPayload {
  'type': 'export_config';
}
export type { ExportConfigPayload };