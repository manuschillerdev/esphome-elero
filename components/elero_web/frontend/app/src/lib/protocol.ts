import type { RfData } from '@/generated'

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

export const state = {
  UNKNOWN: '0x00',
  TOP: '0x01',
  BOTTOM: '0x02',
  INTERMEDIATE: '0x03',
  TILT: '0x04',
  BLOCKING: '0x05',
  OVERHEATED: '0x06',
  TIMEOUT: '0x07',
  START_MOVING_UP: '0x08',
  START_MOVING_DOWN: '0x09',
  MOVING_UP: '0x0a',
  MOVING_DOWN: '0x0b',
  STOPPED: '0x0d',
  TOP_TILT: '0x0e',
  BOTTOM_TILT: '0x0f',
  LIGHT_OFF: '0x0f',
  LIGHT_ON: '0x10',
} as const

// ─── Label Lookups ──────────────────────────────────────────────────────────

const STATE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(state)
    .filter(([k]) => k !== 'LIGHT_OFF')
    .map(([k, v]) => [v, k])
)
const COMMAND_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(command).map(([k, v]) => [v, k])
)
const MSG_TYPE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(msg_type).map(([k, v]) => [v, k])
)

export function getStateLabel(hexState: string | undefined): string {
  if (!hexState) return 'UNKNOWN'
  return STATE_LABELS[hexState.toLowerCase()] ?? hexState
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

export function isMovingState(hexState: string | undefined): boolean {
  if (!hexState) return false
  const s = hexState.toLowerCase()
  return s === state.START_MOVING_UP || s === state.START_MOVING_DOWN ||
         s === state.MOVING_UP || s === state.MOVING_DOWN
}

// ─── Helpers ────────────────────────────────────────────────────────────────

export function parseFreq(val: number | string | undefined, defaultVal: number): number {
  if (val === undefined) return defaultVal
  if (typeof val === 'number') return val
  return parseInt(val, 16) || defaultVal
}

export type RfPacketWithTimestamp = RfData & { received_at?: number }
