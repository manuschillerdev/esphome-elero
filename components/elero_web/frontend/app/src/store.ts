import { signal, computed, batch } from '@preact/signals'
import type {
  ConfigData, RfData, DeviceType, CrudEventData, DeviceUpsertedData,
  StateChangedData, FreqConfig, HubMode, HubConfig, HubConfigEventData, RadioConfig,
  BlindConfig, LightConfig, RemoteConfig, GroupConfig as WireGroupConfig, GroupRemovedData,
  RfStateName,
  ConfigSnapshot, ImportResult, LearnInStateData,
} from '@/generated'

// Re-export generated types used by components
export type { RfData, DeviceType, BlindConfig, LightConfig, FreqConfig, HubMode, HubConfig, RadioConfig, CrudEventData, DeviceUpsertedData, StateChangedData, RfStateName, LearnInStateData }
export type GroupConfig = WireGroupConfig & { updated_at: number | null }

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

const STATE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(STATE_HEX).map(([, name]) => [name, name.toUpperCase()])
)
const COMMAND_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(command).map(([k, v]) => [v, k])
)
const MSG_TYPE_LABELS: Record<string, string> = Object.fromEntries(
  Object.entries(msg_type).map(([k, v]) => [v, k])
)

// ─── Packet Helpers ──────────────────────────────────────────────────────────

export type RfPacketWithTimestamp = RfData & { received_at?: number }

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

export function parseFreq(val: number | string | undefined, defaultVal: number): number {
  if (val === undefined) return defaultVal
  if (typeof val === 'number') return val
  return parseInt(val, 16) || defaultVal
}

// ─── Device Types ────────────────────────────────────────────────────────────

export type AppDeviceType = 'cover' | 'light' | 'remote' | 'unknown'

export interface DevicePairing {
  remote: string
  channel: number
}

export interface Device {
  address: string
  type: DeviceType
  updated_at: number | null  // non-null = saved (server-confirmed), null = unsaved
  enabled: boolean
  channel: number
  remote: string
  pairings: DevicePairing[]
  name: string
  open_ms: number
  close_ms: number
  supports_tilt: boolean
  dim_ms: number
  lastStatus: RfPacketWithTimestamp | null
}

// ─── Primary Signals ────────────────────────────────────────────────────────

export const connected = signal(false)

export const hub = signal<HubConfig>({
  device: '',
  version: '',
  mode: 'native',
  crud: false,
  name: '',
  default_src_address: '0x000001',
})

export const radio = signal<RadioConfig>({
  chipset: 'cc1101',
  rx_sensitivity: -104,
  freq: { freq0: '0x7a', freq1: '0x71', freq2: '0x21' },
})

export const devices = signal<Map<string, Device>>(new Map())

export const groups = signal<Map<string, GroupConfig>>(new Map())

export const rfPackets = signal<RfPacketWithTimestamp[]>([])

/// True when NVS config has changed and a reboot is needed to apply in HA (native mode)
export const rebootNeeded = signal(false)

export const learnIn = signal<LearnInStateData>({
  state: 'idle',
  active: false,
  busy: false,
})

export type ActiveTab = 'manage' | 'packets' | 'hub'

export const activeTab = signal<ActiveTab>('manage')

// ─── Computed (auto-tracked, auto-memoized) ─────────────────────────────────

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

function makePairings(remote: string | undefined, channel: number | undefined): DevicePairing[] {
  return remote ? [{ remote, channel: channel ?? 0 }] : []
}

function mergePairings(...sources: Array<DevicePairing[] | undefined>): DevicePairing[] {
  const result: DevicePairing[] = []
  const seen = new Set<string>()
  for (const source of sources) {
    for (const pairing of source ?? []) {
      if (!pairing.remote) continue
      const key = `${pairing.remote}:${pairing.channel}`
      if (seen.has(key)) continue
      seen.add(key)
      result.push(pairing)
    }
  }
  return result
}

function makeDevice(partial: Partial<Device> & { address: string; type: DeviceType }): Device {
  return {
    updated_at: null,
    enabled: true,
    channel: 0,
    remote: '',
    pairings: [],
    name: '',
    open_ms: 0,
    close_ms: 0,
    supports_tilt: false,
    dim_ms: 0,
    lastStatus: null,
    ...partial,
  }
}

function blindToDevice(b: BlindConfig): Device {
  return makeDevice({
    address: b.address, type: 'cover', updated_at: b.updated_at || null, enabled: b.enabled,
    name: b.name, channel: b.channel, remote: b.remote, pairings: makePairings(b.remote, b.channel),
    open_ms: b.open_ms, close_ms: b.close_ms, supports_tilt: b.supports_tilt,
    lastStatus: b.state && b.state !== '0x00'
      ? { state: b.state, rssi: b.rssi } as RfPacketWithTimestamp
      : null,
  })
}

function lightToDevice(l: LightConfig): Device {
  return makeDevice({
    address: l.address, type: 'light', updated_at: l.updated_at || null, enabled: l.enabled,
    name: l.name, channel: l.channel, remote: l.remote, pairings: makePairings(l.remote, l.channel), dim_ms: l.dim_ms,
    lastStatus: l.state && l.state !== '0x00'
      ? { state: l.state, rssi: l.rssi } as RfPacketWithTimestamp
      : null,
  })
}

function remoteToDevice(r: RemoteConfig): Device {
  return makeDevice({
    address: r.address, type: 'remote', updated_at: r.updated_at || null, name: r.name,
  })
}

function groupToApp(group: WireGroupConfig, updated_at: number | null = Date.now()): GroupConfig {
  return { ...group, updated_at }
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
    next.set(b.address, {
      ...existing,
      ...device,
      remote: existing?.remote || device.remote,
      channel: existing?.remote ? existing.channel : device.channel,
      pairings: mergePairings(existing?.pairings, device.pairings),
      lastStatus: existing?.lastStatus ?? device.lastStatus,
    })
  }
  for (const l of data.lights) {
    const device = lightToDevice(l)
    const existing = next.get(l.address)
    next.set(l.address, {
      ...existing,
      ...device,
      remote: existing?.remote || device.remote,
      channel: existing?.remote ? existing.channel : device.channel,
      pairings: mergePairings(existing?.pairings, device.pairings),
      lastStatus: existing?.lastStatus ?? device.lastStatus,
    })
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
    groups.value = new Map((data.groups ?? []).map((group) => [group.id, groupToApp(group)]))
    hub.value = data.hub
    radio.value = data.radio
    learnIn.value = { state: 'idle', active: false, busy: false }
  })
}

export function updateDevice(address: string, updates: Partial<Device>) {
  const d = devices.value.get(address)
  if (!d) return
  const next = new Map(devices.value)
  const updated = { ...d, updated_at: null, ...updates }
  if ('remote' in updates || 'channel' in updates) {
    updated.pairings = mergePairings(d.pairings, makePairings(updated.remote, updated.channel))
  }
  next.set(address, updated)
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

  if (t === msg_type.COMMAND || t === msg_type.COMMAND_ALT) {
    const target = (next ?? devs).get(pkt.dst)
    const pairing = makePairings(pkt.src, pkt.channel)
    if (!target) {
      mut().set(pkt.dst, makeDevice({ address: pkt.dst, type: 'cover', remote: pkt.src, channel: pkt.channel, pairings: pairing }))
    } else if (target.type !== 'remote') {
      mut().set(pkt.dst, {
        ...target,
        remote: target.remote || pkt.src,
        channel: target.remote ? target.channel : pkt.channel,
        pairings: mergePairings(target.pairings, pairing),
      })
    }
    if (!(next ?? devs).has(pkt.src)) mut().set(pkt.src, makeDevice({ address: pkt.src, type: 'remote' }))
  } else if (t === msg_type.STATUS || t === msg_type.STATUS_ALT) {
    const existing = (next ?? devs).get(pkt.src)
    if (existing) {
      // Type correction: if we see a light state, correct cover→light
      const name = resolveStateName(pkt.state)
      const correctedType: DeviceType =
        (name === 'light_on' || name === 'bottom_tilt') ? 'light' : existing.type
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

  const device = makeDevice({
    address: data.address,
    type: data.device_type,
    updated_at: data.updated_at || null,
    enabled: data.enabled ?? true,
    name: data.name ?? '',
    channel: data.channel ?? 0,
    remote: data.remote ?? '',
    pairings: makePairings(data.remote, data.channel),
    open_ms: data.open_ms ?? 0,
    close_ms: data.close_ms ?? 0,
    supports_tilt: data.supports_tilt ?? false,
    dim_ms: data.dim_ms ?? 0,
    lastStatus: existing?.lastStatus ?? null,
  })

  next.set(data.address, {
    ...device,
    pairings: mergePairings(existing?.pairings, device.pairings),
  })

  // Ensure remote entry exists for non-remote devices
  if (data.device_type !== 'remote' && data.remote && !next.has(data.remote)) {
    next.set(data.remote, makeDevice({ address: data.remote, type: 'remote' }))
  }

  devices.value = next

  if (hub.value.mode === 'native') {
    rebootNeeded.value = true
  }
}

export function onStateChanged(data: StateChangedData) {
  const existing = devices.value.get(data.address)
  if (!existing) return

  const next = new Map(devices.value)

  // Build a synthetic lastStatus from the snapshot so the UI updates immediately.
  // This is the optimistic update — overridden by the next real RF packet.
  const lastStatus = {
    ...existing.lastStatus,
    state: data.state,
    ha_state: data.ha_state,
    rssi: data.rssi,
    received_at: Date.now(),
  } as RfPacketWithTimestamp

  next.set(data.address, { ...existing, lastStatus })
  devices.value = next
}

export function onHubConfig(data: HubConfigEventData) {
  hub.value = { ...hub.value, name: data.name }
}

export function onLearnInState(data: LearnInStateData) {
  learnIn.value = data
}

export function onGroupUpserted(data: WireGroupConfig) {
  const next = new Map(groups.value)
  next.set(data.id, groupToApp(data))
  groups.value = next
}

export function onGroupRemoved(data: GroupRemovedData) {
  const next = new Map(groups.value)
  next.delete(data.id)
  groups.value = next
}

export function updateGroup(id: string, updates: Partial<GroupConfig>) {
  const group = groups.value.get(id)
  if (!group) return
  const next = new Map(groups.value)
  next.set(id, { ...group, updated_at: null, ...updates })
  groups.value = next
}

// ─── Toast (one-shot user feedback) ─────────────────────────────────────────

export type ToastVariant = 'info' | 'success' | 'error'
export interface Toast {
  id: number
  variant: ToastVariant
  message: string
}

export const toast = signal<Toast | null>(null)
let nextToastId = 1

export function showToast(variant: ToastVariant, message: string) {
  toast.value = { id: nextToastId++, variant, message }
}

export function dismissToast() {
  toast.value = null
}

// ─── Backup / Restore ───────────────────────────────────────────────────────

function snapshotFilename(snap: ConfigSnapshot): string {
  const device = snap.exporter?.device || hub.value.device || 'elero-gateway'
  const stamp = new Date().toISOString().replace(/[:.]/g, '-').replace('T', '_').slice(0, 19)
  return `${device}-backup-${stamp}.json`
}

export function onConfigSnapshot(snap: ConfigSnapshot) {
  // Trigger a download via Blob + anchor click
  const json = JSON.stringify(snap, null, 2)
  const blob = new Blob([json], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = snapshotFilename(snap)
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
  URL.revokeObjectURL(url)
  const groupCount = snap.groups?.length ?? 0
  showToast('success', `Backup downloaded (${snap.devices.length} device${snap.devices.length === 1 ? '' : 's'}, ${groupCount} group${groupCount === 1 ? '' : 's'})`)
}

export function onImportResult(result: ImportResult) {
  const total = result.added + result.updated + result.skipped + result.groups_added + result.groups_updated + result.groups_skipped
  const parts: string[] = []
  if (result.added > 0) parts.push(`${result.added} device${result.added === 1 ? '' : 's'} added`)
  if (result.updated > 0) parts.push(`${result.updated} device${result.updated === 1 ? '' : 's'} updated`)
  if (result.skipped > 0) parts.push(`${result.skipped} device${result.skipped === 1 ? '' : 's'} skipped`)
  if (result.groups_added > 0) parts.push(`${result.groups_added} group${result.groups_added === 1 ? '' : 's'} added`)
  if (result.groups_updated > 0) parts.push(`${result.groups_updated} group${result.groups_updated === 1 ? '' : 's'} updated`)
  if (result.groups_skipped > 0) parts.push(`${result.groups_skipped} group${result.groups_skipped === 1 ? '' : 's'} skipped`)
  if (result.hub_applied) parts.push('hub config restored')
  const summary = parts.length > 0 ? parts.join(', ') : 'no changes'
  if (result.errors.length > 0) {
    const first = result.errors[0]
    showToast('error', `Import: ${summary} — ${result.errors.length} error${result.errors.length === 1 ? '' : 's'} (e.g. #${first.index}: ${first.msg})`)
  } else if (total === 0 && !result.hub_applied) {
    showToast('info', 'Import: empty snapshot, nothing applied')
  } else {
    showToast('success', `Import: ${summary}`)
  }
}

export function onDeviceRemoved({ address }: CrudEventData) {
  const next = new Map(devices.value)
  next.delete(address)
  const nextGroups = new Map<string, GroupConfig>()
  for (const [id, group] of groups.value) {
    const device_ids = group.device_ids.filter((deviceId) => deviceId !== address)
    if (device_ids.length >= 2) nextGroups.set(id, { ...group, device_ids })
  }
  batch(() => {
    devices.value = next
    groups.value = nextGroups
  })

  if (hub.value.mode === 'native') {
    rebootNeeded.value = true
  }
}

export function setActiveTab(tab: ActiveTab) {
  activeTab.value = tab
}
