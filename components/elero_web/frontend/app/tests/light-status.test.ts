import assert from 'node:assert/strict'
import { beforeEach, test } from 'node:test'
import { addRfPacket, devices, rfPackets, resolveStateName as storeStateName } from '../src/store'
import { getStateLabel, resolveStateName } from '../src/lib/protocol'
import type { RfData } from '../src/generated'

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
  assert.equal(getStateLabel('0x0f', 'cover'), 'BOTTOM_TILT')
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
