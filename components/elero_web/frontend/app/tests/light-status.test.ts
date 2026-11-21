import assert from 'node:assert/strict'
import { beforeEach, test } from 'node:test'
import { addRfPacket, devices, onStateChanged, rfPackets, resolveStateName as storeStateName } from '../src/store'
import { packetCommandLabel, getStateLabel, resolveStateName } from '../src/lib/protocol'
import { envelope } from '../mock-contract'
import type { RfData, StateChangedData } from '../src/generated'

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
  assert.equal(getStateLabel('0x0f', 'cover'), 'BOTTOM + INTERMEDIATE')
})

await test('Sombrero status bytes have a display label and a valid snapshot name', () => {
  const cases = [
    ['0x0c', 'delete_in_remote'], ['0x12', 'scene_set'], ['0x13', 'scene_started'],
    ['0x14', 'scene_stopped'], ['0x15', 'scene_error'],
  ] as const
  for (const [byte, state] of cases) {
    assert.equal(resolveStateName(byte), state)
    assert.equal(getStateLabel(byte), state.toUpperCase())
    assert.doesNotThrow(() => envelope('state_changed', { address, device_type: 'cover', state }))
  }
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

await test('packet details distinguish releases, polls, and group observations', () => {
  for (const [type, key, label] of [
    ['0x44', '0x00', '0x00 RELEASE'],
    ['0x6a', '0x00', '0x00 CHECK'],
    ['0x45', '0x10', '0x10 Gesture unknown'],
  ]) {
    assert.equal(packetCommandLabel({ ...packet, type, command: key }), label)
  }
  const raw = { ...packet, src: '0x888888', dst: '0x999999', decoded: false, command: undefined, state: undefined }
  assert.doesNotThrow(() => envelope('rf', raw))
  addRfPacket(raw)
  assert.equal(rfPackets.value.at(-1), raw)
  assert.equal(devices.value.has(raw.src), false)
  assert.equal(devices.value.has(raw.dst), false)
  assert.equal(packetCommandLabel(raw), '')
})
