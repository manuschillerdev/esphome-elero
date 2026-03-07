import { create } from 'zustand'

// ─── Types from WebSocket events ─────────────────────────────────────────────

export interface BlindConfig {
  address: string
  name: string
  channel: number
  remote: string
  open_ms: number
  close_ms: number
  poll_ms: number
  tilt?: boolean
}

export interface LightConfig {
  address: string
  name: string
  channel: number
  remote: string
  dim_ms: number
}

// ─── Protocol Constants (mirrors C++ packet:: namespace in elero_packet.h) ───

// packet::msg_type — message type byte
export const msg_type = {
  BUTTON: '0x44',          // Button press/release (broadcast)
  COMMAND: '0x6a',         // Targeted command to blind
  COMMAND_ALT: '0x69',     // Alternate command format
  STATUS: '0xca',          // Status response from blind
  STATUS_ALT: '0xc9',      // Alternate status format
} as const

// packet::command — command byte
export const command = {
  CHECK: '0x00',           // Request status (no movement)
  STOP: '0x10',            // Stop movement
  UP: '0x20',              // Move up / open
  TILT: '0x24',            // Tilt position
  DOWN: '0x40',            // Move down / close
  INTERMEDIATE: '0x44',    // Move to intermediate position
} as const

// packet::state — state byte (from status packets)
export const state = {
  UNKNOWN: '0x00',
  TOP: '0x01',             // Fully open position
  BOTTOM: '0x02',          // Fully closed position
  INTERMEDIATE: '0x03',    // Intermediate position
  TILT: '0x04',            // Tilted position
  BLOCKING: '0x05',        // Obstacle detected
  OVERHEATED: '0x06',      // Motor overheated
  TIMEOUT: '0x07',         // Communication timeout
  START_MOVING_UP: '0x08', // Starting upward movement
  START_MOVING_DOWN: '0x09', // Starting downward movement
  MOVING_UP: '0x0a',       // Currently moving up
  MOVING_DOWN: '0x0b',     // Currently moving down
  STOPPED: '0x0d',         // Stopped (after movement)
  TOP_TILT: '0x0e',        // Open + tilted
  BOTTOM_TILT: '0x0f',     // Closed + tilted / Light off
  LIGHT_OFF: '0x0f',       // Light off (alias for BOTTOM_TILT)
  LIGHT_ON: '0x10',        // Light on
} as const

// Reverse lookup: hex value → constant name
const STATE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(state)
    .filter(([k]) => k !== 'LIGHT_OFF') // skip alias, BOTTOM_TILT takes priority
    .map(([k, v]) => [v, k])
)

const COMMAND_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(command).map(([k, v]) => [v, k])
)

const MSG_TYPE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(msg_type).map(([k, v]) => [v, k])
)

// ─── Packet Helpers ──────────────────────────────────────────────────────────

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

export function isStatusPacket(pkt: RfPacket): boolean {
  const t = pkt.type?.toLowerCase()
  return t === msg_type.STATUS || t === msg_type.STATUS_ALT
}

export function isCommandPacket(pkt: RfPacket): boolean {
  const t = pkt.type?.toLowerCase()
  return t === msg_type.COMMAND || t === msg_type.COMMAND_ALT
}

export function isButtonPacket(pkt: RfPacket): boolean {
  return pkt.type?.toLowerCase() === msg_type.BUTTON
}

export function isMovingState(hexState: string | undefined): boolean {
  if (!hexState) return false
  const s = hexState.toLowerCase()
  return s === state.START_MOVING_UP || s === state.START_MOVING_DOWN ||
         s === state.MOVING_UP || s === state.MOVING_DOWN
}

export interface FreqConfig {
  freq0?: number | string
  freq1?: number | string
  freq2?: number | string
}

export function parseFreq(val: number | string | undefined, defaultVal: number): number {
  if (val === undefined) return defaultVal
  if (typeof val === 'number') return val
  return parseInt(val, 16) || defaultVal
}

export interface DeviceConfig {
  device: string
  blinds: BlindConfig[]
  lights: LightConfig[]
  freq: FreqConfig
}

export interface RfPacket {
  t: number        // timestamp (ms)
  src: string      // source address
  dst: string      // destination address
  channel: number  // RF channel
  type: string     // msg_type: BUTTON, COMMAND, STATUS, etc.
  type2: string    // secondary type byte
  command: string  // command byte (hex string)
  state: string    // state byte (hex string)
  echo: boolean    // true if retransmitted packet
  cnt: number      // packet counter (for dedup)
  rssi: number     // signal strength (dBm)
  hop: string      // hop count (hex string)
  raw: string      // raw packet bytes (hex string with spaces)
  received_at?: number  // client-side timestamp (Date.now())
}

// Device type derived from packet behavior
export type DeviceType = 'blind' | 'light' | 'remote' | 'unknown'

// Derive device type from message type and state
function deriveDeviceType(packets: RfPacket[], address: string): DeviceType {
  for (const pkt of packets) {
    if (pkt.src === address && isButtonPacket(pkt)) {
      return 'remote'
    }
    if (pkt.src === address && isStatusPacket(pkt)) {
      return pkt.state?.toLowerCase() === state.LIGHT_ON ? 'light' : 'blind'
    }
  }
  return 'unknown'
}

export interface LogEntry {
  t: number
  level: number    // 1=error, 2=warn, 3+=info
  tag: string
  msg: string
}

// ─── UI State ────────────────────────────────────────────────────────────────

export type FilterState = 'all' | 'configured' | 'discovered'
export type DeviceTypeFilter = 'all' | 'blinds' | 'lights'

// ─── localStorage helpers for remote names ───────────────────────────────────

const REMOTE_NAMES_KEY = 'elero:remoteNames'

function loadRemoteNames(): Record<string, string> {
  try {
    return JSON.parse(localStorage.getItem(REMOTE_NAMES_KEY) || '{}')
  } catch {
    return {}
  }
}

function saveRemoteNames(names: Record<string, string>) {
  localStorage.setItem(REMOTE_NAMES_KEY, JSON.stringify(names))
}

// ─── Store ───────────────────────────────────────────────────────────────────

interface AppState {
  // Connection
  connected: boolean

  // Config from server (received on connect)
  config: DeviceConfig

  // RF state - derived from rf events
  states: Record<string, RfPacket>  // address -> last packet
  rfPackets: RfPacket[]             // packet history (last 200)
  rfFilter: string

  // Logs
  logs: LogEntry[]

  // Remote names (client-side, persisted to localStorage)
  remoteNames: Record<string, string>

  // UI state
  activeTab: 'devices' | 'packets' | 'hub'
  filter: FilterState
  deviceTypeFilter: DeviceTypeFilter

  // Actions - connection
  setConnected: (connected: boolean) => void
  setConfig: (config: DeviceConfig) => void

  // Actions - config updates
  updateBlind: (address: string, updates: Partial<BlindConfig>) => void
  updateLight: (address: string, updates: Partial<LightConfig>) => void
  setRemoteName: (address: string, name: string) => void

  // Actions - RF
  addRfPacket: (pkt: RfPacket) => void
  setRfFilter: (filter: string) => void
  clearRfPackets: () => void

  // Actions - logs
  addLog: (log: LogEntry) => void
  clearLogs: () => void

  // Actions - UI
  setActiveTab: (tab: AppState['activeTab']) => void
  setFilter: (filter: FilterState) => void
  setDeviceTypeFilter: (filter: DeviceTypeFilter) => void
}

const DEFAULT_CONFIG: DeviceConfig = {
  device: '',
  blinds: [],
  lights: [],
  freq: {},
}

export const useStore = create<AppState>((set) => ({
  // Connection
  connected: false,
  config: DEFAULT_CONFIG,

  // RF state
  states: {},
  rfPackets: [],
  rfFilter: '',

  // Logs
  logs: [],

  // Remote names
  remoteNames: loadRemoteNames(),

  // UI state
  activeTab: 'devices',
  filter: 'all',
  deviceTypeFilter: 'all',

  // Actions - connection
  setConnected: (connected) => set({ connected }),
  setConfig: (config) => set({ config }),

  // Actions - config updates
  updateBlind: (address, updates) => set((s) => ({
    config: {
      ...s.config,
      blinds: s.config.blinds.map((b) =>
        b.address === address ? { ...b, ...updates } : b
      ),
    },
  })),
  updateLight: (address, updates) => set((s) => ({
    config: {
      ...s.config,
      lights: s.config.lights.map((l) =>
        l.address === address ? { ...l, ...updates } : l
      ),
    },
  })),
  setRemoteName: (address, name) => set((s) => {
    const remoteNames = { ...s.remoteNames, [address]: name }
    saveRemoteNames(remoteNames)
    return { remoteNames }
  }),

  // Actions - RF — only status packets (STATUS/STATUS_ALT) update device state
  addRfPacket: (pkt) => set((s) => {
    const t = pkt.type?.toLowerCase()
    const isStatus = t === msg_type.STATUS || t === msg_type.STATUS_ALT
    return {
      states: isStatus ? { ...s.states, [pkt.src]: pkt } : s.states,
      rfPackets: [...s.rfPackets, pkt].slice(-200),
    }
  }),
  setRfFilter: (rfFilter) => set({ rfFilter }),
  clearRfPackets: () => set({ rfPackets: [], states: {} }),

  // Actions - logs
  addLog: (log) => set((s) => ({
    logs: [...s.logs, log].slice(-100),
  })),
  clearLogs: () => set({ logs: [] }),

  // Actions - UI
  setActiveTab: (activeTab) => set({ activeTab }),
  setFilter: (filter) => set({ filter }),
  setDeviceTypeFilter: (deviceTypeFilter) => set({ deviceTypeFilter }),
}))

// ─── Derived Helpers ────────────────────────────────────────────────────────
// Pure functions that derive state from specific inputs. Call during render
// with primitives selected from the store — never pass to useStore() directly.

/** address → display name (from config + remote names) */
export function buildConfigNames(blinds: BlindConfig[], lights: LightConfig[], remoteNames: Record<string, string>): Record<string, string> {
  const map: Record<string, string> = {}
  blinds.forEach((b) => {
    map[b.address] = b.name
    if (!map[b.remote]) map[b.remote] = remoteNames[b.remote] || b.remote
  })
  lights.forEach((l) => {
    map[l.address] = l.name
    if (!map[l.remote]) map[l.remote] = remoteNames[l.remote] || l.remote
  })
  return map
}

/** address → DeviceType (from config knowledge + packet inference) */
export function buildAddressTypes(blinds: BlindConfig[], lights: LightConfig[], rfPackets: RfPacket[]): Record<string, DeviceType> {
  const map: Record<string, DeviceType> = {}
  const remoteAddrs = new Set<string>()
  const blindAddrs = new Set<string>()
  const lightAddrs = new Set<string>()
  blinds.forEach((b) => { blindAddrs.add(b.address); remoteAddrs.add(b.remote) })
  lights.forEach((l) => { lightAddrs.add(l.address); remoteAddrs.add(l.remote) })

  const seen = new Set<string>()
  rfPackets.forEach((p) => { seen.add(p.src); seen.add(p.dst) })
  seen.forEach((addr) => {
    if (remoteAddrs.has(addr)) map[addr] = 'remote'
    else if (blindAddrs.has(addr)) map[addr] = 'blind'
    else if (lightAddrs.has(addr)) map[addr] = 'light'
    else map[addr] = deriveDeviceType(rfPackets, addr)
  })
  return map
}

/** Filter counts for control bar */
export function buildFilterCounts(blinds: BlindConfig[], lights: LightConfig[], states: Record<string, RfPacket>) {
  const configuredAddrs = new Set([
    ...blinds.map((b) => b.address),
    ...lights.map((l) => l.address),
  ])
  const discoveredCount = Object.keys(states).filter((addr) => !configuredAddrs.has(addr)).length
  return {
    all: blinds.length + lights.length + discoveredCount,
    configured: blinds.length + lights.length,
    discovered: discoveredCount,
    blinds: blinds.length,
    lights: lights.length,
  }
}

/** Discovered addresses (in states but not in config) */
export function buildDiscoveredAddresses(blinds: BlindConfig[], lights: LightConfig[], states: Record<string, RfPacket>): string[] {
  const configuredAddrs = new Set([
    ...blinds.map((b) => b.address),
    ...lights.map((l) => l.address),
  ])
  return Object.keys(states).filter((addr) => !configuredAddrs.has(addr))
}

export interface RemoteGroupData {
  address: string
  blinds: BlindConfig[]
  lights: LightConfig[]
}

/** Configured devices grouped by remote address, respecting device type filter */
export function buildRemoteGroups(blinds: BlindConfig[], lights: LightConfig[], deviceTypeFilter: DeviceTypeFilter): RemoteGroupData[] {
  const groups = new Map<string, RemoteGroupData>()
  const addGroup = (addr: string) => {
    if (!groups.has(addr)) groups.set(addr, { address: addr, blinds: [], lights: [] })
    return groups.get(addr)!
  }
  if (deviceTypeFilter === 'all' || deviceTypeFilter === 'blinds') {
    for (const blind of blinds) addGroup(blind.remote).blinds.push(blind)
  }
  if (deviceTypeFilter === 'all' || deviceTypeFilter === 'lights') {
    for (const light of lights) addGroup(light.remote).lights.push(light)
  }
  return Array.from(groups.values())
}
