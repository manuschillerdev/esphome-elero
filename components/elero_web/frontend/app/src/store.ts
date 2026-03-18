import { signal, computed, batch } from '@preact/signals'
import type {
  ConfigData, RfData, DeviceType, CrudEventData, DeviceUpsertedData,
  StateChangedData, FreqConfig, HubMode, BlindConfig, LightConfig, RemoteConfig,
} from '@/generated'

// Re-export generated types used by components
export type { RfData, DeviceType, BlindConfig, LightConfig, FreqConfig, HubMode, CrudEventData, DeviceUpsertedData, StateChangedData }

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

// ─── Packet Helpers ──────────────────────────────────────────────────────────

export type RfPacketWithTimestamp = RfData & { received_at?: number }

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

export function parseFreq(val: number | string | undefined, defaultVal: number): number {
  if (val === undefined) return defaultVal
  if (typeof val === 'number') return val
  return parseInt(val, 16) || defaultVal
}

// ─── Device Types ────────────────────────────────────────────────────────────

export type AppDeviceType = 'cover' | 'light' | 'remote' | 'unknown'

export interface Device {
  address: string
  type: DeviceType
  updated_at: number | null  // non-null = saved (server-confirmed), null = unsaved
  enabled: boolean
  channel: number
  remote: string
  name: string
  open_ms: number
  close_ms: number
  poll_ms: number
  supports_tilt: boolean
  dim_ms: number
  lastStatus: RfPacketWithTimestamp | null
}

export interface DeviceGroup {
  remote: Device
  devices: Device[]
}

// ─── Primary Signals ────────────────────────────────────────────────────────

export const connected = signal(false)

export const hub = signal<{
  device: string
  version: string
  freq: FreqConfig
  mode: HubMode
  crud: boolean
}>({
  device: '',
  version: '',
  freq: { freq0: '0x7a', freq1: '0x71', freq2: '0x21' },
  mode: 'native',
  crud: false,
})

export const devices = signal<Map<string, Device>>(new Map())

export const rfPackets = signal<RfPacketWithTimestamp[]>([])

export type StatusFilter = 'all' | 'saved' | 'unsaved'
export type DeviceTypeFilter = 'all' | 'covers' | 'lights'
export type ActiveTab = 'devices' | 'packets' | 'hub'

export const ui = signal<{
  activeTab: ActiveTab
  filters: {
    status: StatusFilter
    deviceType: DeviceTypeFilter
    rf: string
  }
}>({
  activeTab: 'devices',
  filters: { status: 'all', deviceType: 'all', rf: '' },
})

// ─── Computed (auto-tracked, auto-memoized) ─────────────────────────────────

export const deviceGroups = computed<DeviceGroup[]>(() => {
  const devs = devices.value
  const { status, deviceType } = ui.value.filters
  const groups = new Map<string, Device[]>()
  for (const d of devs.values()) {
    if (d.type === 'remote') continue
    if (status === 'saved' && d.updated_at === null) continue
    if (status === 'unsaved' && d.updated_at !== null) continue
    if (deviceType === 'covers' && d.type !== 'cover') continue
    if (deviceType === 'lights' && d.type !== 'light') continue
    const key = d.remote || 'unknown'
    const arr = groups.get(key)
    if (arr) arr.push(d)
    else groups.set(key, [d])
  }
  return [...groups]
    .sort(([a], [b]) => a.localeCompare(b))
    .map(([addr, items]) => ({
      remote: devs.get(addr) ?? makeDevice({ address: addr, type: 'remote' }),
      devices: items.sort((a, b) => a.address.localeCompare(b.address)),
    }))
})

export const filterCounts = computed(() => {
  let saved = 0, unsaved = 0, covers = 0, lights = 0
  for (const d of devices.value.values()) {
    if (d.type === 'remote') continue
    if (d.updated_at !== null) saved++; else unsaved++
    if (d.type === 'cover') covers++
    if (d.type === 'light') lights++
  }
  return { all: saved + unsaved, saved, unsaved, covers, lights }
})

export const displayNames = computed<Record<string, string>>(() => {
  const result: Record<string, string> = {}
  for (const [addr, d] of devices.value) result[addr] = d.name || addr
  return result
})

export const deviceTypeMap = computed<Record<string, AppDeviceType>>(() => {
  const result: Record<string, AppDeviceType> = {}
  for (const [addr, d] of devices.value) result[addr] = d.type
  return result
})

// ─── Device Factories ────────────────────────────────────────────────────────

function makeDevice(partial: Partial<Device> & { address: string; type: DeviceType }): Device {
  return {
    updated_at: null,
    enabled: true,
    channel: 0,
    remote: '',
    name: '',
    open_ms: 0,
    close_ms: 0,
    poll_ms: 0,
    supports_tilt: false,
    dim_ms: 0,
    lastStatus: null,
    ...partial,
  }
}

function blindToDevice(b: BlindConfig): Device {
  return makeDevice({
    address: b.address, type: 'cover', updated_at: b.updated_at ?? null, enabled: b.enabled,
    name: b.name, channel: b.channel, remote: b.remote,
    open_ms: b.open_ms, close_ms: b.close_ms, poll_ms: b.poll_ms, supports_tilt: b.supports_tilt,
    lastStatus: b.state && b.state !== '0x00'
      ? { state: b.state, rssi: b.rssi } as RfPacketWithTimestamp
      : null,
  })
}

function lightToDevice(l: LightConfig): Device {
  return makeDevice({
    address: l.address, type: 'light', updated_at: l.updated_at ?? null, enabled: l.enabled,
    name: l.name, channel: l.channel, remote: l.remote, dim_ms: l.dim_ms,
    lastStatus: l.state && l.state !== '0x00'
      ? { state: l.state, rssi: l.rssi } as RfPacketWithTimestamp
      : null,
  })
}

function remoteToDevice(r: RemoteConfig): Device {
  return makeDevice({
    address: r.address, type: 'remote', updated_at: r.updated_at ?? null, name: r.name,
  })
}

// ─── Actions ─────────────────────────────────────────────────────────────────

export function setConnected(val: boolean) {
  connected.value = val
}

export function setDevices(data: ConfigData) {
  const next = new Map(devices.value)
  for (const b of data.blinds) {
    const device = blindToDevice(b)
    const existing = next.get(b.address)
    next.set(b.address, { ...device, lastStatus: existing?.lastStatus ?? device.lastStatus })
  }
  for (const l of data.lights) {
    const device = lightToDevice(l)
    const existing = next.get(l.address)
    next.set(l.address, { ...device, lastStatus: existing?.lastStatus ?? device.lastStatus })
  }
  for (const r of data.remotes ?? []) {
    const existing = next.get(r.address)
    if (!existing || existing.updated_at === null) {
      next.set(r.address, { ...remoteToDevice(r), lastStatus: existing?.lastStatus ?? null })
    }
  }
  for (const b of data.blinds) {
    if (!next.has(b.remote)) next.set(b.remote, makeDevice({ address: b.remote, type: 'remote' }))
  }
  for (const l of data.lights) {
    if (!next.has(l.remote)) next.set(l.remote, makeDevice({ address: l.remote, type: 'remote' }))
  }
  batch(() => {
    devices.value = next
    hub.value = {
      device: data.device,
      version: data.version,
      freq: data.freq,
      mode: data.mode ?? 'native',
      crud: data.crud ?? false,
    }
  })
}

export function updateDevice(address: string, updates: Partial<Device>) {
  const d = devices.value.get(address)
  if (!d) return
  const next = new Map(devices.value)
  next.set(address, { ...d, updated_at: null, ...updates })
  devices.value = next
}

export function addRfPacket(pkt: RfPacketWithTimestamp) {
  const t = pkt.type?.toLowerCase()
  const devs = devices.value
  let next: Map<string, Device> | null = null

  const mut = () => {
    if (!next) next = new Map(devs)
    return next
  }

  if ((t === msg_type.COMMAND || t === msg_type.COMMAND_ALT) && !pkt.echo) {
    if (!devs.has(pkt.dst)) mut().set(pkt.dst, makeDevice({ address: pkt.dst, type: 'cover', remote: pkt.src, channel: pkt.channel }))
    if (!(next ?? devs).has(pkt.src)) mut().set(pkt.src, makeDevice({ address: pkt.src, type: 'remote' }))
  } else if (t === msg_type.STATUS || t === msg_type.STATUS_ALT) {
    const existing = (next ?? devs).get(pkt.src)
    if (existing) {
      // Type correction: if we see a light state, correct cover→light
      const s = pkt.state?.toLowerCase()
      const correctedType: DeviceType =
        (s === state.LIGHT_ON || s === state.LIGHT_OFF) ? 'light' : existing.type
      mut().set(pkt.src, { ...existing, type: correctedType, lastStatus: pkt })
    }
    // Do NOT create devices from status packets — byte offset 6 is not the RF channel.
    // Discovery happens from COMMAND packets only (which carry correct channel).
  } else if (t === msg_type.BUTTON) {
    if (!devs.has(pkt.src)) mut().set(pkt.src, makeDevice({ address: pkt.src, type: 'remote' }))
  }

  batch(() => {
    rfPackets.value = [...rfPackets.value, pkt]
    if (next) devices.value = next
  })
}

export function clearRfPackets() {
  const next = new Map<string, Device>()
  for (const [addr, d] of devices.value) {
    next.set(addr, d.lastStatus ? { ...d, lastStatus: null } : d)
  }
  batch(() => {
    rfPackets.value = []
    devices.value = next
  })
}

export function onDeviceUpserted(data: DeviceUpsertedData) {
  const existing = devices.value.get(data.address)
  const next = new Map(devices.value)

  next.set(data.address, makeDevice({
    address: data.address,
    type: data.device_type,
    updated_at: data.updated_at ?? null,
    enabled: data.enabled ?? true,
    name: data.name ?? '',
    channel: data.channel ?? 0,
    remote: data.remote ?? '',
    open_ms: data.open_ms ?? 0,
    close_ms: data.close_ms ?? 0,
    poll_ms: data.poll_ms ?? 0,
    supports_tilt: data.supports_tilt ?? false,
    dim_ms: data.dim_ms ?? 0,
    lastStatus: existing?.lastStatus ?? null,
  }))

  // Ensure remote entry exists for non-remote devices
  if (data.device_type !== 'remote' && data.remote && !next.has(data.remote)) {
    next.set(data.remote, makeDevice({ address: data.remote, type: 'remote' }))
  }

  devices.value = next
}

export function onStateChanged(data: StateChangedData) {
  const existing = devices.value.get(data.address)
  if (!existing) return

  const next = new Map(devices.value)

  // Build a synthetic lastStatus from the snapshot so the UI updates immediately.
  // This is the optimistic update — overridden by the next real RF packet.
  const lastStatus: RfPacketWithTimestamp = {
    ...(existing.lastStatus ?? {} as RfPacketWithTimestamp),
    state: data.state,
    rssi: data.rssi,
    received_at: Date.now(),
  } as RfPacketWithTimestamp

  next.set(data.address, { ...existing, lastStatus })
  devices.value = next
}

export function onDeviceRemoved({ address }: CrudEventData) {
  const next = new Map(devices.value)
  next.delete(address)
  devices.value = next
}

export function setActiveTab(tab: ActiveTab) {
  ui.value = { ...ui.value, activeTab: tab }
}

export function setStatusFilter(status: StatusFilter) {
  ui.value = { ...ui.value, filters: { ...ui.value.filters, status } }
}

export function setDeviceTypeFilter(deviceType: DeviceTypeFilter) {
  ui.value = { ...ui.value, filters: { ...ui.value.filters, deviceType } }
}

export function setRfFilter(rf: string) {
  ui.value = { ...ui.value, filters: { ...ui.value.filters, rf } }
}
