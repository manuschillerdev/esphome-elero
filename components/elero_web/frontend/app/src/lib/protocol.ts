import { rfStatusNames } from '../generated/rf-status-names'
import type { DeviceType, FreqConfig, RfData, RfStateName } from '@/generated'

import { command, msg_type } from '../generated/rf-wire'
export { command, msg_type }

/** Commands that indicate a real user action (not a status poll). */
export const DISCOVERY_COMMANDS: Set<string> = new Set([
  command.UP, command.DOWN, command.FULL_UP, command.FULL_DOWN, command.STOP, command.TILT, command.INTERMEDIATE,
])

const legacyNames = new Map(rfStatusNames.map((name, byte) => [`0x${byte.toString(16).padStart(2, '0')}`, name]))

export function resolveStateName(raw: string | undefined): RfStateName | undefined {
  return raw ? legacyNames.get(raw.toLowerCase()) ?? rfStatusNames.find(name => name === raw) : undefined
}

// ─── Label Lookups ──────────────────────────────────────────────────────────

const STATE_LABELS: Record<string, string> = {
  ...Object.fromEntries(rfStatusNames.map(name => [name, name.toUpperCase()])),
  delete_in_remote: 'Delete in remote',
  bottom_tilt: 'Bottom (also intermediate position)',
  scene_set: 'Scene set',
  scene_started: 'Scene started',
  scene_stopped: 'Scene stopped',
  scene_error: 'Scene error',
}
const MSG_TYPE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(msg_type).map(([k, v]) => [v, k])
)

export function getStateLabel(state: string | undefined, deviceType?: DeviceType): string {
  if (!state) return 'UNKNOWN'
  const name = resolveStateName(state)
  // Switching states are distinct from the cover endpoint at 0x0f.
  if (deviceType === 'light') {
    if (name === 'light_off') return 'OFF'
    if (name === 'light_on') return 'ON'
  }
  return (name && STATE_LABELS[name]) ?? state
}

export function getCommandLabel(pkt: Pick<RfData, 'command' | 'command_name'>): string {
  return pkt.command_name?.toUpperCase() ?? pkt.command
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

export function isSelectorPacket(pkt: RfData): boolean {
  const t = pkt.type?.toLowerCase()
  return t === msg_type.BUTTON || t === msg_type.BUTTON_GROUP || t === msg_type.PROGRAM
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
  const parsed = parseInt(val, 16)
  return Number.isFinite(parsed) ? parsed : defaultVal
}

/** All drivers accept this CC1101-format word with a 26 MHz reference. */
export function formatFrequency(freq: FreqConfig): string {
  const word = parseFreq(freq.freq2, 0x21) * 65536
    + parseFreq(freq.freq1, 0x71) * 256 + parseFreq(freq.freq0, 0x7a)
  return `${(word * 26 / 65536).toFixed(3)} MHz`
}

export type RfPacketWithTimestamp = RfData & { received_at?: number }
