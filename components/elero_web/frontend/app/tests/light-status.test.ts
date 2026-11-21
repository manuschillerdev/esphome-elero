import assert from 'node:assert/strict'
import { beforeEach, test } from 'node:test'
import { addRfPacket, devices, onStateChanged, rfPackets, setDevices, resolveStateName as storeStateName } from '../src/store'
import { getStateLabel, resolveStateName } from '../src/lib/protocol'
import { envelope } from '../mock-contract'
import type { ConfigData, RfData, StateChangedData } from '../src/generated'

const remote = '0x111111'
const address = '0x222222'
const packet: RfData = {
  t: 100, src: remote, dst: address, channel: 1, type: '0x6a', type2: '0x00',
  command: '0x20', state: '0x00', cnt: 1, rssi: -50, hop: '0x0a', raw: '',
}

beforeEach(() => {
  devices.value = new Map()
  rfPackets.value = []
  addRfPacket(packet)
})

await test('literal switching states have the correct labels in both decoders', () => {
  for (const resolve of [resolveStateName, storeStateName]) {
    assert.equal(resolve('0x10'), 'light_off')
    assert.equal(resolve('0x11'), 'light_on')
    assert.equal(resolve('0x0f'), 'bottom_tilt')
  }
  assert.equal(getStateLabel('0x10', 'light'), 'OFF')
  assert.equal(getStateLabel('0x11', 'light'), 'ON')
  assert.equal(getStateLabel('0x0f', 'cover'), 'Bottom (also intermediate position)')
})

await test('raw reports, snapshots and repeated reports show the same receiver status', () => {
  const cases = [
    ['0x0f', 'bottom_tilt', 'Bottom (also intermediate position)'],
    ['0x0c', 'delete_in_remote', 'Delete in remote'], ['0x12', 'scene_set', 'Scene set'],
    ['0x13', 'scene_started', 'Scene started'], ['0x14', 'scene_stopped', 'Scene stopped'],
    ['0x15', 'scene_error', 'Scene error'],
  ] as const
  for (const [byte, state, label] of cases) {
    const report: RfData = { ...packet, type: '0xca', src: address, dst: remote, state: byte, state_name: state }
    envelope('rf', report)
    addRfPacket(report)
    assert.equal(getStateLabel(devices.value.get(address)?.lastStatus?.state), label)
    assert.equal(getStateLabel(byte), label) // Byte-only report from older firmware.
    const snapshot: StateChangedData = { address, device_type: 'cover', state, ha_state: 'closed' }
    envelope('state_changed', snapshot)
    onStateChanged(snapshot)
    addRfPacket(report)
    const last = devices.value.get(address)?.lastStatus
    assert.equal(last?.ha_state, 'closed')
    assert.equal(getStateLabel(last?.state), label)
    assert.equal(rfPackets.value.at(-1)?.state, byte)
  }
})

await test('reconnect replaces a stale status with the current firmware snapshot', () => {
  onStateChanged({ address, device_type: 'cover', state: 'scene_started', ha_state: 'open' })
  const config: ConfigData = {
    hub: { device: 'esp32', version: 'test', name: 'test', mode: 'mqtt', crud: true, default_src_address: remote },
    radio: { chipset: 'cc1101', rx_sensitivity: -104, freq: { freq0: '0x7a', freq1: '0x71', freq2: '0x21' } },
    blinds: [{ address, remote, channel: 1, name: 'Cover', enabled: true,
      open_ms: 0, close_ms: 0, poll_ms: 0, supports_tilt: true, state: 'scene_error', ha_state: 'closed' }],
    lights: [], groups: [], remotes: [],
  }
  envelope('config', config)
  setDevices(config)
  const last = devices.value.get(address)?.lastStatus
  assert.equal(last?.ha_state, 'closed')
  assert.equal(getStateLabel(last?.state), 'Scene error')
})

await test('a cover endpoint report does not change the discovered device to a light', () => {
  addRfPacket({ ...packet, src: address, dst: remote, type: '0xca', state: '0x0f' })
  assert.equal(devices.value.get(address)?.type, 'cover')
})

await test('both literal switching state bytes identify a light receiver', () => {
  for (const state of ['0x10', '0x11']) {
    devices.value = new Map()
    addRfPacket(packet)
    addRfPacket({ ...packet, src: address, dst: remote, type: '0xca', state })
    assert.equal(devices.value.get(address)?.type, 'light')
  }
})

await test('contract light snapshots drive the displayed state without aliases', () => {
  for (const state of ['light_off', 'light_on'] as const) {
    const data: StateChangedData = { address, device_type: 'light', state }
    assert.doesNotThrow(() => envelope('state_changed', data))
    onStateChanged(data)
    assert.equal(devices.value.get(address)?.lastStatus?.state, state)
    assert.equal(getStateLabel(devices.value.get(address)?.lastStatus?.state, 'light'),
      state === 'light_off' ? 'OFF' : 'ON')
  }
})

await test('the contract rejects noncanonical switching snapshot names', () => {
  for (const state of ['off', 'on']) {
    const data = { address, device_type: 'light', state } as StateChangedData
    assert.throws(() => envelope('state_changed', data), /allowed values/)
  }
})
