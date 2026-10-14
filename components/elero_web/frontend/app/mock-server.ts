import { createInterface } from 'node:readline'
import { WebSocketServer, WebSocket } from 'ws'
import { envelope, readCommand, type ClientMessage, type Events } from './mock-contract'
import type {
  ConfigData, DeviceSnapshot, DeviceAction, DeviceUpsertedData, DeviceType,
  GroupConfig, GroupStateData, StateChangedData, RfStateName, RfData,
  LearnInStateData, LearnInState, ConfigSnapshot, ImportResult,
} from './src/generated/index'

// This simulates a gateway for UI development, not RF framing/encryption or motor firmware.
const PORT = Number(process.env.MOCK_PORT ?? 8080)
const DEFAULT_NAME = 'Mock Elero Gateway'
const STEP_MS = 300
const hex = (value: number, width = 2) => `0x${value.toString(16).padStart(width, '0')}`
const stateBytes: Record<RfStateName, number> = {
  unknown: 0, top: 1, bottom: 2, intermediate: 3, tilt: 4, blocking: 5,
  overheated: 6, timeout: 7, start_moving_up: 8, start_moving_down: 9,
  moving_up: 10, moving_down: 11, stopped: 13, top_tilt: 14, bottom_tilt: 15, light_on: 16,
}
const defaults = { command_up: '0x20', command_down: '0x40', command_stop: '0x10', command_check: '0x00', command_tilt: '0x24' }
type Motion = { from: number; target: number; started: number; duration: number }
type Device = {
  config: DeviceSnapshot; value: number; state: RfStateName; motion?: Motion;
  source: 'unknown' | 'hub' | 'remote'; lastSeen: number; rssi: number; updatedAt: number; published?: string;
}
let boot = Date.now()
let counter = 0
let nameOverride = ''
const now = () => Date.now() - boot
const devices = new Map<string, Device>()
const groups = new Map<string, GroupConfig>()
const publishedGroups = new Map<string, string>()
let learn: LearnInStateData = { state: 'idle', active: false, busy: false }
let learnStep: ReturnType<typeof setTimeout> | undefined
let learnTimeout: ReturnType<typeof setTimeout> | undefined
let restarting = false

for (const [index, entry] of [
  ['0x313238', 'Terrasse', 2, false], ['0x333238', 'Lichthof OG', 7, true],
  ['0x413238', 'Terrasse Küche', 3, false], ['0x4d5748', 'Schlafzimmer', 33, true],
].entries()) {
  const [address, name, channel, tilt] = entry as [string, string, number, boolean]
  seed({ device_type: 'cover', dst_address: address, name, channel, supports_tilt: tilt,
    src_address: index % 2 ? '0x28b864' : '0x17a753', open_duration_ms: 25000, close_duration_ms: 25000, ...defaults })
}
seed({ device_type: 'light', dst_address: '0xc41a2b', name: 'Gartenleuchte', channel: 5, src_address: '0x17a753', dim_duration_ms: 0 })
seed({ device_type: 'light', dst_address: '0xd52b3c', name: 'Terrassenspot', channel: 8, src_address: '0x28b864', dim_duration_ms: 5000 })
seed({ device_type: 'remote', dst_address: '0x17a753', name: 'Fernbedienung 1' })
seed({ device_type: 'remote', dst_address: '0x28b864', name: 'Fernbedienung 2' })
groups.set('grp_terrasse_k8x2p1', { id: 'grp_terrasse_k8x2p1', name: 'Terrasse', device_ids: ['0x313238', '0x413238'] })

function seed(config: DeviceSnapshot): Device {
  const device: Device = { config: { enabled: true, ...config }, value: config.device_type === 'cover' ? 0.5 : 0,
    state: config.device_type === 'cover' ? 'stopped' : config.device_type === 'light' ? 'bottom_tilt' : 'unknown',
    source: 'unknown', lastSeen: 0, rssi: -70, updatedAt: 1 }
  devices.set(config.dst_address, device)
  return device
}
function address(value: string): string {
  if (!/^0x[\da-f]{1,6}$/i.test(value) || Number(value) === 0) throw new Error('Address must be a nonzero 3-byte hex string')
  return hex(Number(value), 6)
}
function validateDevice(input: DeviceSnapshot): DeviceSnapshot {
  const config = { ...input, dst_address: address(input.dst_address) }
  if (config.name && Buffer.byteLength(config.name) > 23) throw new Error('Device name exceeds 23 bytes')
  if (config.device_type !== 'remote') {
    config.src_address = address(config.src_address ?? '0x000001')
    config.channel ??= 1
    if (!Number.isInteger(config.channel) || config.channel < 1 || config.channel > 255) throw new Error('Channel must be 1..255')
  }
  for (const key of ['open_duration_ms', 'close_duration_ms', 'dim_duration_ms'] as const) {
    const value = config[key]
    if (value !== undefined && (!Number.isInteger(value) || value < 0 || value > 0xffffffff)) throw new Error(`Invalid ${key}`)
  }
  for (const key of ['hop', 'payload_1', 'payload_2', 'msg_type', 'type2', ...Object.keys(defaults)] as const) {
    const value = config[key as keyof DeviceSnapshot]
    if (value !== undefined && (typeof value !== 'string' || !/^0x[\da-f]{1,2}$/i.test(value))) throw new Error(`Invalid byte: ${key}`)
  }
  return config
}
function configEvent(device: Device): DeviceUpsertedData {
  const c = device.config
  const common = { address: c.dst_address, device_type: c.device_type, name: c.name ?? '', updated_at: device.updatedAt }
  if (c.device_type === 'remote') return common
  const rf = { ...common, channel: c.channel ?? 1, remote: c.src_address ?? '0x000001', enabled: c.enabled ?? true }
  if (c.device_type === 'light') return { ...rf, dim_ms: c.dim_duration_ms ?? 0 }
  return { ...rf, open_ms: c.open_duration_ms ?? 0, close_ms: c.close_duration_ms ?? 0, poll_ms: 300000,
    supports_tilt: c.supports_tilt ?? false, ...Object.fromEntries(Object.keys(defaults).map(key => [key, c[key as keyof DeviceSnapshot] ?? defaults[key as keyof typeof defaults]])) }
}
function snapshot(device: Device): StateChangedData {
  const c = device.config
  const problem = ['blocking', 'overheated', 'timeout'].includes(device.state)
  const common: StateChangedData = { address: c.dst_address, device_type: c.device_type,
    state: device.state, command_source: device.source, rssi: device.rssi, last_seen: device.lastSeen,
    is_problem: problem, problem_type: problem ? device.state as 'blocking' | 'overheated' | 'timeout' : 'none' }
  if (c.device_type === 'remote') return common
  if (c.device_type === 'light') return { ...common, is_on: device.value > 0, brightness: device.value }
  return { ...common, position: device.value, device_class: 'shutter', tilted: device.state.includes('tilt'),
    ha_state: device.motion ? device.motion.target > device.value ? 'opening' : 'closing'
      : device.value === 0 ? 'closed' : device.value === 1 ? 'open' : 'stopped' }
}
function groupState(group: GroupConfig): GroupStateData {
  const members = group.device_ids.map(id => devices.get(id))
  const first = members[0]
  const available = members.every(d => d && d.config.device_type !== 'remote' && d.config.enabled !== false && d.config.device_type === first?.config.device_type)
  const opening = members.some(d => d?.config.device_type === 'cover' && d.motion && d.motion.target > d.value)
  const closing = members.some(d => d?.config.device_type === 'cover' && d.motion && d.motion.target < d.value)
  return { id: group.id, available, device_type: first?.config.device_type ?? 'cover',
    supports_position: available && members.every(d => d?.config.device_type === 'cover' && (d.config.open_duration_ms ?? 0) > 0 && (d.config.close_duration_ms ?? 0) > 0),
    position: available ? Math.round(members.reduce((sum, d) => sum + (d?.value ?? 0), 0) / members.length * 100) / 100 : 0,
    operation: available ? opening ? 'opening' : closing ? 'closing' : 'idle' : 'idle',
    is_on: available && members.some(d => d?.config.device_type === 'light' && d.value > 0) }
}
function config(): ConfigData {
  const data: ConfigData = { hub: { device: 'lilygo-t-embed', version: '0.0.0-mock', mode: 'mqtt', crud: true,
    name: nameOverride || DEFAULT_NAME, default_src_address: '0x000001' },
    radio: { chipset: 'cc1101', rx_sensitivity: -104, freq: { freq0: '0x7a', freq1: '0x71', freq2: '0x21' } },
    blinds: [], lights: [], remotes: [], groups: [...groups.values()].map(g => ({ ...g, state: groupState(g) })) }
  for (const device of devices.values()) {
    const c = configEvent(device)
    const { device_type: type, ...entry } = c
    const live = { rssi: device.rssi, last_seen: device.lastSeen, state: hex(stateBytes[device.state]) }
    if (type === 'cover') data.blinds.push({ ...entry, name: c.name ?? '', channel: c.channel!, remote: c.remote!, enabled: c.enabled!,
      open_ms: c.open_ms!, close_ms: c.close_ms!, poll_ms: c.poll_ms!, supports_tilt: c.supports_tilt!, ...live, position: device.value })
    else if (type === 'light') data.lights.push({ ...entry, name: c.name ?? '', channel: c.channel!, remote: c.remote!, enabled: c.enabled!, dim_ms: c.dim_ms!, ...live, brightness: device.value, is_on: device.value > 0 })
    else data.remotes.push({ address: entry.address, name: entry.name ?? '', updated_at: entry.updated_at })
  }
  return data
}
const wss = new WebSocketServer({ port: PORT, path: '/elero/ws', maxPayload: 1024 * 1024 })
function send<K extends keyof Events>(ws: WebSocket, event: K, data: Events[K]) {
  if (ws.readyState === WebSocket.OPEN) ws.send(envelope(event, data))
}
function broadcast<K extends keyof Events>(event: K, data: Events[K]) {
  const text = envelope(event, data)
  for (const ws of wss.clients) if (ws.readyState === WebSocket.OPEN) ws.send(text)
}
function log(msg: string, level = 3) { broadcast('log', { t: now(), level, tag: 'elero.mock', msg }) }
function publish() {
  for (const d of devices.values()) {
    const data = snapshot(d)
    const text = JSON.stringify(data)
    if (text !== d.published) { d.published = text; broadcast('state_changed', data) }
  }
  for (const g of groups.values()) {
    const data = groupState(g)
    const text = JSON.stringify(data)
    if (text !== publishedGroups.get(g.id)) { publishedGroups.set(g.id, text); broadcast('group_state_changed', data) }
  }
}
function rf(device: Device, command = '0x00', transmitting = false, overrides: Partial<RfData> = {}) {
  const c = device.config
  const data: RfData = { t: now(), src: transmitting ? c.src_address ?? '0x000001' : c.dst_address,
    dst: transmitting ? c.dst_address : c.src_address ?? '0x000001', channel: c.channel ?? 1,
    type: transmitting ? '0x44' : '0xca', type2: transmitting ? '0x10' : '0x00', command,
    state: transmitting ? '0x00' : hex(stateBytes[device.state]), cnt: counter = (counter % 255) + 1,
    rssi: device.rssi, hop: c.hop ?? '0x0a', raw: '', ...overrides }
  // Diagnostic bytes only, deliberately not presented as an encrypted over-the-air packet.
  data.raw = [data.cnt, data.channel, Number(data.type), Number(data.type2), Number(data.command), Number(data.state)]
    .map(byte => hex(byte).slice(2)).join(' ')
  broadcast('rf', data)
  if (!transmitting) device.lastSeen = data.t
}
function settle(device: Device) {
  const motion = device.motion
  if (!motion) return
  const fraction = Math.min(1, (Date.now() - motion.started) / motion.duration)
  device.value = Math.round((motion.from + (motion.target - motion.from) * fraction) * 100) / 100
  if (fraction === 1) {
    device.motion = undefined
    device.state = device.config.device_type === 'light' ? device.value > 0 ? 'light_on' : 'bottom_tilt'
      : device.value === 1 ? 'top' : device.value === 0 ? 'bottom' : 'intermediate'
    rf(device)
  }
}
function actionAllowed(device: Device, action: DeviceAction) {
  const c = device.config
  const allowed = c.device_type === 'cover' ? ['up', 'open', 'down', 'close', 'stop', 'check', 'int', 'tilt']
    : c.device_type === 'light' ? ['on', 'off', 'stop', 'check', 'dim_up', 'dim_down'] : []
  if (!allowed.includes(action)) throw new Error(`Unsupported ${c.device_type} action: ${action}`)
  if (action === 'tilt' && !c.supports_tilt) throw new Error('Cover does not support tilt')
  if (action.startsWith('dim_') && !c.dim_duration_ms) throw new Error('Light does not support dimming')
}
function commandByte(device: Device, action: DeviceAction): string {
  const keys: Partial<Record<DeviceAction, keyof typeof defaults>> = { up: 'command_up', open: 'command_up', down: 'command_down', close: 'command_down',
    stop: 'command_stop', check: 'command_check', tilt: 'command_tilt' }
  const key = keys[action]
  if (device.config.device_type === 'cover' && key) return device.config[key] ?? defaults[key]
  return ({ up: '0x20', open: '0x20', down: '0x40', close: '0x40', stop: '0x10', check: '0x00',
    tilt: '0x24', on: '0x20', off: '0x40', dim_up: '0x20', dim_down: '0x40', int: '0x44' })[action]
}
function act(device: Device, action: DeviceAction, source: Device['source'] = 'hub', emitCommand = true) {
  actionAllowed(device, action)
  settle(device)
  if (emitCommand) rf(device, commandByte(device, action), true, source === 'remote' ? { type: '0x44' } : {})
  if (action === 'check') { rf(device); publish(); return }
  device.source = source
  device.motion = undefined
  if (action === 'stop') device.state = device.config.device_type === 'cover' ? 'stopped' : device.value > 0 ? 'light_on' : 'bottom_tilt'
  else if (action === 'tilt') device.state = device.value === 1 ? 'top_tilt' : device.value === 0 ? 'bottom_tilt' : 'tilt'
  else if (action === 'on' || action === 'off') { device.value = action === 'on' ? 1 : 0; device.state = action === 'on' ? 'light_on' : 'bottom_tilt' }
  else {
    const target = action === 'int' ? 0.5 : ['up', 'open', 'dim_up'].includes(action) ? 1 : 0
    const duration = device.config.device_type === 'light' ? device.config.dim_duration_ms ?? 0
      : target > device.value ? device.config.open_duration_ms ?? 0 : device.config.close_duration_ms ?? 0
    if (target !== device.value) {
      // Without travel calibration, use a simulated 25-second motor duration.
      device.motion = { from: device.value, target, started: Date.now(), duration: Math.max(STEP_MS, (duration || 25000) * Math.abs(target - device.value)) }
      device.state = device.config.device_type === 'cover' ? target > device.value ? 'moving_up' : 'moving_down' : 'light_on'
    }
  }
  rf(device); publish()
}
function find(value: string, type?: DeviceType): Device {
  const d = devices.get(address(value))
  if (!d || (type && d.config.device_type !== type)) throw new Error('Unknown device')
  return d
}
function upsert(input: DeviceSnapshot, preserveCommands = false): boolean {
  const c = validateDevice(input)
  const existing = devices.get(c.dst_address)
  if (existing && existing.config.device_type !== c.device_type) throw new Error('Address already belongs to a different device type')
  if (!existing && devices.size >= 48) throw new Error('No free device slot')
  if (preserveCommands && existing?.config.device_type === 'cover') {
    for (const key of Object.keys(defaults) as (keyof typeof defaults)[]) c[key] ??= existing.config[key]
  }
  const d = existing ?? seed(c)
  d.config = { enabled: true, ...c }
  d.updatedAt = Math.max(1, now())
  d.motion = undefined
  broadcast('device_upserted', configEvent(d)); publish()
  return !!existing
}
function upsertGroup(input: GroupConfig): boolean {
  const group = { id: input.id, name: input.name, device_ids: input.device_ids.map(address) }
  if (!group.id || Buffer.byteLength(group.id) > 23 || !group.name || Buffer.byteLength(group.name) > 23) throw new Error('Invalid group id/name')
  if (!group.device_ids.length || group.device_ids.length > 48 || new Set(group.device_ids).size !== group.device_ids.length) throw new Error('Invalid group members')
  const members = group.device_ids.map(id => find(id))
  if (members.some(d => d.config.device_type === 'remote' || d.config.device_type !== members[0].config.device_type)) throw new Error('Group members must be all covers or all lights')
  const perRemote = new Map<string, number>()
  for (const d of members) { const src = d.config.src_address!; perRemote.set(src, (perRemote.get(src) ?? 0) + 1) }
  if ([...perRemote.values()].some(count => count > 31)) throw new Error('Too many members for one remote')
  const exists = groups.has(group.id)
  if (!exists && groups.size >= 16) throw new Error('No free group slot')
  groups.set(group.id, group); broadcast('group_upserted', group); publish()
  return exists
}
function removeGroup(id: string) {
  if (!groups.delete(id)) throw new Error('Unknown group')
  publishedGroups.delete(id); broadcast('group_removed', { id })
}
function backup(): ConfigSnapshot {
  return { snapshot_version: 2, exported_at: now(), exporter: { device: 'lilygo-t-embed', version: '0.0.0-mock' },
    hub: { name_override: nameOverride }, devices: [...devices.values()].map(d => ({ ...d.config })),
    groups: [...groups.values()].map(g => ({ ...g, device_ids: [...g.device_ids] })) }
}
function hubName(name: string): boolean {
  if (Buffer.byteLength(name) > 31) throw new Error('Hub name exceeds 31 bytes')
  const changed = nameOverride !== name
  nameOverride = name
  if (changed) broadcast('hub_config', { name: nameOverride || DEFAULT_NAME })
  return changed
}
function restore(s: ConfigSnapshot): ImportResult {
  if (!Number.isInteger(s.snapshot_version) || s.snapshot_version < 1 || s.snapshot_version > 2) throw new Error('Unsupported snapshot_version')
  const result: ImportResult = { added: 0, updated: 0, skipped: 0, groups_added: 0, groups_updated: 0, groups_skipped: 0, hub_applied: false, errors: [] }
  if (s.hub.name_override !== undefined) result.hub_applied = hubName(s.hub.name_override)
  s.devices.forEach((d, index) => {
    try { result[upsert(d) ? 'updated' : 'added']++ }
    catch (error) { result.skipped++; result.errors.push({ index, msg: String(error instanceof Error ? error.message : error) }) }
  })
  s.groups.forEach((g, index) => {
    try { result[upsertGroup(g) ? 'groups_updated' : 'groups_added']++ }
    catch (error) { result.groups_skipped++; result.errors.push({ index, msg: String(error instanceof Error ? error.message : error) }) }
  })
  return result
}
function learnState(state: LearnInState) {
  const active = ['programming', 'wait_up', 'confirming_up', 'wait_down', 'confirming_down'].includes(state)
  learn = { ...learn, state, active, busy: ['programming', 'confirming_up', 'confirming_down'].includes(state) }
  if (!active) { clearTimeout(learnStep); clearTimeout(learnTimeout) }
  broadcast('learn_in_state', learn); log(`Learn-in: ${state}`)
}
function learnTransition(pending: LearnInState, next: LearnInState) {
  learnState(pending)
  const virtual: Device = { config: { device_type: 'remote', dst_address: '0x000000', src_address: learn.src_address, channel: learn.channel }, value: 0, state: 'unknown', source: 'hub', lastSeen: 0, rssi: -70, updatedAt: 0 }
  rf(virtual, pending === 'programming' ? '0x80' : pending === 'confirming_up' ? '0x20' : '0x40', true,
    { type: pending === 'programming' ? '0x70' : '0x44' })
  learnStep = setTimeout(() => learnState(process.env.MOCK_LEARN_IN_FAILURE === '1' ? 'failed' : next), STEP_MS)
}
function handle(ws: WebSocket, m: ClientMessage) {
  if (restarting) throw new Error('Gateway is restarting')
  switch (m.type) {
    case 'cmd': act(find(m.address), m.action); break
    case 'raw': {
      const src = address(m.src_address), dst = address(m.dst_address)
      if (m.channel !== undefined && (m.channel < 0 || m.channel > 255)) throw new Error('Raw channel must be 0..255')
      for (const key of ['command', 'payload_1', 'payload_2', 'msg_type', 'type2', 'hop'] as const) {
        if (m[key] !== undefined && !/^0x[\da-f]{1,2}$/i.test(m[key]!)) throw new Error(`Invalid raw byte: ${key}`)
      }
      const target = devices.get(dst)
      rf(target ?? { config: { device_type: 'cover', dst_address: dst, src_address: src }, value: 0, state: 'unknown', source: 'hub', lastSeen: 0, rssi: -70, updatedAt: 0 }, m.command ?? '0x00', true,
        { src, dst, channel: m.channel ?? 0, type: m.msg_type ?? '0x6a', type2: m.type2 ?? '0x00', hop: m.hop ?? '0x0a' })
      if (target) {
        const actions: DeviceAction[] = target.config.device_type === 'cover' ? ['check', 'stop', 'up', 'down', 'tilt', 'int'] : ['check', 'stop', 'on', 'off', 'dim_up', 'dim_down']
        const action = actions.find(a => Number(commandByte(target, a)) === Number(m.command ?? '0x00'))
        if (action) act(target, action, 'hub', false)
      }
      break
    }
    case 'upsert_device': { const { type: _, ...d } = m; upsert(d, true); break }
    case 'remove_device': {
      const d = find(m.dst_address, m.device_type)
      devices.delete(d.config.dst_address)
      broadcast('device_removed', { address: d.config.dst_address, device_type: m.device_type })
      for (const g of groups.values()) {
        if (!g.device_ids.includes(d.config.dst_address)) continue
        const ids = g.device_ids.filter(id => id !== d.config.dst_address)
        if (ids.length) upsertGroup({ ...g, device_ids: ids }); else removeGroup(g.id)
      }
      publish(); break
    }
    case 'upsert_group': upsertGroup(m); break
    case 'remove_group': removeGroup(m.id); break
    case 'group_cmd': {
      const g = groups.get(m.id)
      if (!g) throw new Error('Unknown group')
      if (!groupState(g).available) throw new Error('Group is unavailable')
      const members = g.device_ids.map(id => find(id))
      for (const d of members) actionAllowed(d, m.action)
      const buckets = new Map<string, Device[]>()
      for (const d of members) {
        const src = d.config.src_address!
        const bucket = buckets.get(src) ?? []
        bucket.push(d); buckets.set(src, bucket)
      }
      for (const bucket of buckets.values()) {
        if (bucket.some(d => commandByte(d, m.action) !== commandByte(bucket[0], m.action))) {
          for (const d of bucket) act(d, m.action)
          continue
        }
        // Firmware sends a selector group packet per source address (singletons use normal TX).
        rf(bucket[0], commandByte(bucket[0], m.action), true, bucket.length > 1
          ? { channel: 0, dst: '0x000000', destinations: [] } : {})
        for (const d of bucket) act(d, m.action, 'hub', false)
      }
      break
    }
    case 'set_hub_config': hubName(m.name); break
    case 'export_config': send(ws, 'config_snapshot', backup()); break
    case 'import_config': send(ws, 'import_result', restore(m.snapshot)); break
    case 'learn_in_start':
      if (learn.active) throw new Error('Learn-in already active')
      if (m.session_timeout_ms !== undefined && (m.session_timeout_ms <= 0 || m.session_timeout_ms > 0x7fffffff)) throw new Error('Invalid session timeout')
      learn = { ...learn, src_address: address(m.src_address), channel: m.channel }
      learnTransition('programming', 'wait_up')
      learnTimeout = setTimeout(() => learnState('timed_out'), m.session_timeout_ms ?? 300000)
      break
    case 'learn_in_confirm_up':
      if (learn.state !== 'wait_up') throw new Error('Learn-in is not waiting for up confirmation')
      learnTransition('confirming_up', 'wait_down'); break
    case 'learn_in_confirm_down':
      if (learn.state !== 'wait_down') throw new Error('Learn-in is not waiting for down confirmation')
      learnTransition('confirming_down', 'complete'); break
    case 'learn_in_cancel': if (learn.active) learnState('cancelled'); else send(ws, 'learn_in_state', learn); break
    case 'restart':
      restarting = true; log('Restarting gateway')
      clearTimeout(learnStep); clearTimeout(learnTimeout)
      for (const d of devices.values()) { settle(d); d.motion = undefined; d.source = 'unknown'; d.lastSeen = 0; d.published = undefined; d.state = d.config.device_type === 'cover' ? 'stopped' : d.state }
      setTimeout(() => {
        boot = Date.now(); counter = 0; learn = { state: 'idle', active: false, busy: false }; publishedGroups.clear()
        for (const client of wss.clients) client.close(1012, 'Mock gateway restart')
        restarting = false
      }, 150)
      break
    default: { const unhandled: never = m; throw new Error(`Unhandled command: ${JSON.stringify(unhandled)}`) }
  }
  log(`Handled ${m.type}`)
}
wss.on('connection', ws => {
  send(ws, 'config', config())
  send(ws, 'learn_in_state', learn)
  for (const d of devices.values()) { send(ws, 'state_changed', snapshot(d)); if (d.config.device_type !== 'remote') rf(d) }
  for (const g of groups.values()) send(ws, 'group_state_changed', groupState(g))
  log('Client connected')
  ws.on('message', (data, binary) => {
    try {
      if (binary) throw new Error('Expected a JSON text message')
      const bytes = Array.isArray(data) ? Buffer.concat(data) : Buffer.isBuffer(data) ? data : Buffer.from(data)
      handle(ws, readCommand(bytes.toString('utf8')))
    } catch (error) { send(ws, 'error', { msg: error instanceof Error ? error.message : String(error) }) }
  })
  ws.on('error', error => console.error('Mock client:', error.message))
})
const tick = setInterval(() => {
  if (restarting) return
  for (const d of devices.values()) settle(d)
  publish()
}, 100)
const poll = setInterval(() => {
  if (!restarting) { for (const d of devices.values()) if (d.config.device_type !== 'remote') rf(d); publish() }
}, 5000)

// Local scenario controls stay off the device WebSocket contract.
// Feed newline-delimited JSON on stdin: {"address":"0x333238","state":"blocking"}
// or {"address":"0x333238","action":"up"} to simulate a remote button press.
const controls = createInterface({ input: process.stdin })
controls.on('line', line => {
  try {
    const c = JSON.parse(line) as { address: string; state?: RfStateName; action?: DeviceAction; rssi?: number }
    const d = find(c.address)
    if (c.action) act(d, c.action, 'remote')
    else {
      if (!c.state || !(c.state in stateBytes)) throw new Error('Expected a documented RF state name')
      if (c.rssi !== undefined && (!Number.isFinite(c.rssi) || c.rssi < -130 || c.rssi > 0)) throw new Error('RSSI must be -130..0')
      settle(d); d.motion = undefined; d.state = c.state
      if (c.state === 'top' || c.state === 'top_tilt' || c.state === 'light_on') d.value = 1
      if (c.state === 'bottom' || c.state === 'bottom_tilt') d.value = 0
      if (['moving_up', 'start_moving_up', 'moving_down', 'start_moving_down'].includes(c.state)) {
        const target = c.state.endsWith('_up') ? 1 : 0
        d.motion = { from: d.value, target, started: Date.now(), duration: Math.max(STEP_MS, 25000 * Math.abs(target - d.value)) }
      }
      if (c.rssi !== undefined) d.rssi = c.rssi
      rf(d); publish(); log(`Injected ${c.state} for ${c.address}`)
    }
  } catch (error) { console.error('Mock scenario:', error instanceof Error ? error.message : error) }
})
function shutdown() {
  clearInterval(tick); clearInterval(poll); clearTimeout(learnStep); clearTimeout(learnTimeout)
  controls.close()
  for (const ws of wss.clients) ws.terminate()
  wss.close()
}
process.on('SIGINT', shutdown)
process.on('SIGTERM', shutdown)
wss.on('error', error => { console.error(error.message); shutdown(); process.exitCode = 1 })
console.log(`Mock gateway: ws://localhost:${PORT}/elero/ws (shared state; configuration persists until process exit)`)
