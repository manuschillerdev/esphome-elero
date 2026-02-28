import { create } from 'zustand'

export type DeviceStatus = 'configured' | 'disabled' | 'discovered'
export type FilterState = 'all' | 'configured' | 'disabled' | 'discovered'
export type ViewMode = 'grid' | 'list'
export type SendStatus = 'idle' | 'sending' | 'success' | 'error'

export interface BlindDevice {
  name: string
  address: string
  channel: number
  rssi: number
  position: number
  status: DeviceStatus
  durationUp: number
  durationDown: number
}

export interface CC1101Registers {
  freq0: string
  freq1: string
  freq2: string
}

export interface DebugPayload {
  blindAddress: string
  channel: string
  remoteAddress: string
  payload1: string
  payload2: string
  pckInf1: string
  pckInf2: string
  hop: string
  commandCheck: string
  commandStop: string
  commandUp: string
  commandDown: string
  commandTilt: string
}

const DEFAULT_REGISTERS: CC1101Registers = { freq0: '0x71', freq1: '0x7A', freq2: '0x21' }

const DEFAULT_PAYLOAD: DebugPayload = {
  blindAddress: '0x893238', channel: '17', remoteAddress: '0x17a753',
  payload1: '0x00', payload2: '0x04', pckInf1: '0x6a', pckInf2: '0x00', hop: '0x0a',
  commandCheck: '0x00', commandStop: '0x10', commandUp: '0x20', commandDown: '0x40', commandTilt: '0x24',
}

const initialDevices: BlindDevice[] = [
  { name: 'Terrasse', address: '0x313238', channel: 2, rssi: -75, position: 0, status: 'configured', durationUp: 24, durationDown: 22 },
  { name: 'Lichthof OG', address: '0x333238', channel: 7, rssi: -75, position: 0, status: 'configured', durationUp: 24, durationDown: 22 },
  { name: 'Terrasse Kuche', address: '0x413238', channel: 3, rssi: -86, position: 0, status: 'disabled', durationUp: 24, durationDown: 22 },
  { name: 'Schlafzimmer', address: '0x4d5748', channel: 33, rssi: -73, position: 0, status: 'disabled', durationUp: 30, durationDown: 28 },
  { name: 'Unknown 0x5a2', address: '0x5a2f10', channel: 12, rssi: -82, position: 0, status: 'discovered', durationUp: 24, durationDown: 22 },
]

interface AppState {
  activeTab: string
  filter: FilterState
  viewMode: ViewMode
  discoveryActive: boolean
  discoveryElapsed: number
  devices: BlindDevice[]
  registers: CC1101Registers
  payload: DebugPayload
  sendStatus: SendStatus
  sentTimestamp: string | null

  setActiveTab: (tab: string) => void
  setFilter: (filter: FilterState) => void
  setViewMode: (mode: ViewMode) => void
  toggleDiscovery: () => void
  incrementDiscoveryElapsed: () => void
  resetDiscoveryElapsed: () => void
  updateDeviceStatus: (address: string, status: DeviceStatus) => void
  renameDevice: (address: string, name: string) => void
  updateDeviceDuration: (address: string, field: 'durationUp' | 'durationDown', value: number) => void
  updateRegister: (key: keyof CC1101Registers, value: string) => void
  resetRegisters: () => void
  updatePayload: (key: keyof DebugPayload, value: string) => void
  resetPayload: () => void
  setSendStatus: (status: SendStatus) => void
  setSentTimestamp: (timestamp: string | null) => void
}

export const useStore = create<AppState>((set) => ({
  activeTab: 'devices',
  filter: 'all',
  viewMode: 'grid',
  discoveryActive: false,
  discoveryElapsed: 0,
  devices: initialDevices,
  registers: DEFAULT_REGISTERS,
  payload: DEFAULT_PAYLOAD,
  sendStatus: 'idle',
  sentTimestamp: null,

  setActiveTab: (tab) => set({ activeTab: tab }),
  setFilter: (filter) => set({ filter }),
  setViewMode: (mode) => set({ viewMode: mode }),
  toggleDiscovery: () => set((s) => ({ discoveryActive: !s.discoveryActive })),
  incrementDiscoveryElapsed: () => set((s) => ({ discoveryElapsed: s.discoveryElapsed + 1 })),
  resetDiscoveryElapsed: () => set({ discoveryElapsed: 0 }),
  updateDeviceStatus: (address, status) => set((s) => ({ devices: s.devices.map((d) => d.address === address ? { ...d, status } : d) })),
  renameDevice: (address, name) => set((s) => ({ devices: s.devices.map((d) => d.address === address ? { ...d, name } : d) })),
  updateDeviceDuration: (address, field, value) => set((s) => ({ devices: s.devices.map((d) => d.address === address ? { ...d, [field]: value } : d) })),
  updateRegister: (key, value) => set((s) => ({ registers: { ...s.registers, [key]: value } })),
  resetRegisters: () => set({ registers: DEFAULT_REGISTERS }),
  updatePayload: (key, value) => set((s) => ({ payload: { ...s.payload, [key]: value } })),
  resetPayload: () => set({ payload: DEFAULT_PAYLOAD, sendStatus: 'idle' }),
  setSendStatus: (status) => set({ sendStatus: status }),
  setSentTimestamp: (timestamp) => set({ sentTimestamp: timestamp }),
}))
