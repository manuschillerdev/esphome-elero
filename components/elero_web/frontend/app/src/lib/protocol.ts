import type { RfData, RfStateName } from '@/generated'

// ─── Protocol Constants (mirrors C++ packet:: namespace in elero_packet.h) ───

export const msg_type = {
  BUTTON: '0x44',
  COMMAND: '0x6a',
  COMMAND_ALT: '0x69',
  STATUS: '0xca',
  STATUS_ALT: '0xc9',
} as const

export const command = {
  CHECK: '0x00',
  STOP: '0x10',
  UP: '0x20',
  TILT: '0x24',
  DOWN: '0x40',
  INTERMEDIATE: '0x44',
} as const

/** Commands that indicate a real user action (not a status poll). */
export const DISCOVERY_COMMANDS: Set<string> = new Set([
  command.UP, command.DOWN, command.STOP, command.TILT, command.INTERMEDIATE,
])

/// Hex byte → RfStateName (from AsyncAPI spec). Single source of truth for
/// normalizing RF packet state bytes to the canonical snake_case names that
/// state_changed events already use.
const STATE_HEX: Record<string, RfStateName> = {
  '0x00': 'unknown',
  '0x01': 'top',
  '0x02': 'bottom',
  '0x03': 'intermediate',
  '0x04': 'tilt',
  '0x05': 'blocking',
  '0x06': 'overheated',
  '0x07': 'timeout',
  '0x08': 'start_moving_up',
  '0x09': 'start_moving_down',
  '0x0a': 'moving_up',
  '0x0b': 'moving_down',
  '0x0d': 'stopped',
  '0x0e': 'top_tilt',
  '0x0f': 'bottom_tilt',
  '0x10': 'light_on',
}

/// Resolve a state value (hex byte or snake_case name) to its canonical RfStateName.
export function resolveStateName(raw: string | undefined): RfStateName | undefined {
  if (!raw) return undefined
  return STATE_HEX[raw.toLowerCase()] ?? (raw as RfStateName)
}

// ─── Label Lookups ──────────────────────────────────────────────────────────

const STATE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(STATE_HEX).map(([, name]) => [name, name.toUpperCase()])
)
const COMMAND_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(command).map(([k, v]) => [v, k])
)
const MSG_TYPE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(msg_type).map(([k, v]) => [v, k])
)

export function getStateLabel(state: string | undefined): string {
  if (!state) return 'UNKNOWN'
  const name = resolveStateName(state)
  return (name && STATE_LABELS[name]) ?? state
}

export function getCommandLabel(hexCmd: string | undefined): string {
  if (!hexCmd) return ''
  return COMMAND_LABELS[hexCmd.toLowerCase()] ?? hexCmd
}

export function getMsgTypeLabel(hexType: string | undefined): string {
  if (!hexType) return ''
  return MSG_TYPE_LABELS[hexType.toLowerCase()] ?? hexType
}

// ─── Packet Type Guards ─────────────────────────────────────────────────────

export function isStatusPacket(pkt: RfData): boolean {
  const t = pkt.type?.toLowerCase()
  return t === msg_type.STATUS || t === msg_type.STATUS_ALT
}

export function isCommandPacket(pkt: RfData): boolean {
  const t = pkt.type?.toLowerCase()
  return t === msg_type.COMMAND || t === msg_type.COMMAND_ALT
}

export function isButtonPacket(pkt: RfData): boolean {
  return pkt.type?.toLowerCase() === msg_type.BUTTON
}

export function isMovingState(raw: string | undefined): boolean {
  const name = resolveStateName(raw)
  return name === 'start_moving_up' || name === 'start_moving_down' ||
         name === 'moving_up' || name === 'moving_down'
}

// ─── Helpers ────────────────────────────────────────────────────────────────

export function parseFreq(val: number | string | undefined, defaultVal: number): number {
  if (val === undefined) return defaultVal
  if (typeof val === 'number') return val
  return parseInt(val, 16) || defaultVal
}

export type RfPacketWithTimestamp = RfData & { received_at?: number }
