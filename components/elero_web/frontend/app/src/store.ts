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

// ─── State Hex Translation ────────────────────────────────────────────────────

// Hex state codes from the protocol (matching C++ ELERO_STATE_* constants)
export const STATE_LABELS: Record<string, string> = {
  '0x00': 'UNKNOWN',
  '0x01': 'TOP',
  '0x02': 'BOTTOM',
  '0x03': 'INTERMEDIATE',
  '0x04': 'TILT',
  '0x05': 'BLOCKING',
  '0x06': 'OVERHEATED',
  '0x07': 'TIMEOUT',
  '0x08': 'START_MOVING_UP',
  '0x09': 'START_MOVING_DOWN',
  '0x0a': 'MOVING_UP',
  '0x0b': 'MOVING_DOWN',
  '0x0d': 'STOPPED',
  '0x0e': 'TOP_TILT',
  '0x0f': 'BOTTOM_TILT',
  '0x10': 'ON',
}

// Get human-readable state label from hex code
export function getStateLabel(hexState: string | undefined): string {
  if (!hexState) return 'unknown'
  const normalized = hexState.toLowerCase()
  return STATE_LABELS[normalized] ?? hexState
}

// Check if state indicates movement
export function isMovingState(hexState: string | undefined): boolean {
  if (!hexState) return false
  const normalized = hexState.toLowerCase()
  return normalized === '0x08' || normalized === '0x09' ||
         normalized === '0x0a' || normalized === '0x0b'
}

export interface FreqConfig {
  freq0?: number | string
  freq1?: number | string
  freq2?: number | string
}

// Parse freq value (handles both number and hex string formats)
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
  type: string     // packet type: 0x44=button, 0x6a=command, 0xca/0xc9=status
  cmd?: string     // command (for outgoing), hex string like "0x20"
  state?: string   // state (for incoming status), hex string like "0x01"
  rssi?: number    // signal strength
  ch?: number      // channel
}

// Device type derived from packet behavior
export type DeviceType = 'blind' | 'light' | 'remote' | 'unknown'

// Light state hex codes
const LIGHT_STATE_ON = '0x10'
const LIGHT_STATE_OFF = '0x0f' // OFF shares value with BOTTOM_TILT

// Derive device type from message type and state
// - 0x44 sender = remote
// - 0xCA/0xC9 sender with ON state (0x10) = light
// - 0xCA/0xC9 sender with other states = blind
export function deriveDeviceType(packets: RfPacket[], address: string): DeviceType {
  for (const pkt of packets) {
    // If this address sends button presses, it's a remote
    if (pkt.src === address && pkt.type === '0x44') {
      return 'remote'
    }
    // If this address sends status responses, check state
    if (pkt.src === address && (pkt.type === '0xca' || pkt.type === '0xc9')) {
      const state = pkt.state?.toLowerCase()
      // ON state (0x10) is unique to lights
      if (state === LIGHT_STATE_ON.toLowerCase()) {
        return 'light'
      }
      // Other states are blinds (0x0f could be either, but blinds are more common)
      return 'blind'
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

export type ViewMode = 'grid' | 'list'
export type FilterState = 'all' | 'configured' | 'discovered'
export type DeviceTypeFilter = 'all' | 'blinds' | 'lights'

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

  // UI state
  activeTab: 'devices' | 'packets' | 'logs' | 'hub'
  viewMode: ViewMode
  filter: FilterState
  deviceTypeFilter: DeviceTypeFilter

  // Actions - connection
  setConnected: (connected: boolean) => void
  setConfig: (config: DeviceConfig) => void

  // Actions - RF
  addRfPacket: (pkt: RfPacket) => void
  setRfFilter: (filter: string) => void
  clearRfPackets: () => void

  // Actions - logs
  addLog: (log: LogEntry) => void
  clearLogs: () => void

  // Actions - UI
  setActiveTab: (tab: AppState['activeTab']) => void
  setViewMode: (mode: ViewMode) => void
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

  // UI state
  activeTab: 'devices',
  viewMode: 'grid',
  filter: 'all',
  deviceTypeFilter: 'all',

  // Actions - connection
  setConnected: (connected) => set({ connected }),
  setConfig: (config) => set({ config }),

  // Actions - RF
  addRfPacket: (pkt) => set((s) => ({
    states: { ...s.states, [pkt.src]: pkt },
    rfPackets: [...s.rfPackets, pkt].slice(-200),
  })),
  setRfFilter: (rfFilter) => set({ rfFilter }),
  clearRfPackets: () => set({ rfPackets: [], states: {} }),

  // Actions - logs
  addLog: (log) => set((s) => ({
    logs: [...s.logs, log].slice(-100),
  })),
  clearLogs: () => set({ logs: [] }),

  // Actions - UI
  setActiveTab: (activeTab) => set({ activeTab }),
  setViewMode: (viewMode) => set({ viewMode }),
  setFilter: (filter) => set({ filter }),
  setDeviceTypeFilter: (deviceTypeFilter) => set({ deviceTypeFilter }),
}))
