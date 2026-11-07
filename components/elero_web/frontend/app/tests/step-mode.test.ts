import assert from 'node:assert/strict'
import { beforeEach, test } from 'node:test'
import { readCommand, envelope } from '../mock-contract'
import { devices, onDeviceUpserted, updateDevice, getCommandLabel as storeCommandLabel } from '../src/store'
import { getCommandLabel } from '../src/lib/protocol'

const address = '0x123456'
beforeEach(() => { devices.value = new Map() })

await test('movement mode survives server config events without replacing custom commands', () => {
  onDeviceUpserted({ address, device_type: 'cover', movement_mode: 'step', command_up: '0x20', command_down: '0x40', updated_at: 1 })
  assert.equal(devices.value.get(address)?.movement_mode, 'step')
  assert.equal(devices.value.get(address)?.command_up, '0x20')
  updateDevice(address, { movement_mode: 'standard' })
  assert.equal(devices.value.get(address)?.movement_mode, 'standard')
  assert.equal(devices.value.get(address)?.updated_at, null)
  onDeviceUpserted({ address, device_type: 'cover', movement_mode: 'standard', updated_at: 2 })
  assert.equal(devices.value.get(address)?.movement_mode, 'standard')
  assert.equal(devices.value.get(address)?.updated_at, 2)
})

await test('older configurations default to standard movement', () => {
  onDeviceUpserted({ address, device_type: 'cover' })
  assert.equal(devices.value.get(address)?.movement_mode, 'standard')
})

await test('contract accepts independent step and full travel actions', () => {
  for (const action of ['step_up', 'step_down', 'full_up', 'full_down']) {
    assert.equal(readCommand(JSON.stringify({ type: 'cmd', address, action })).type, 'cmd')
  }
  assert.equal(getCommandLabel('0x21'), 'FULL_UP')
  assert.equal(getCommandLabel('0x41'), 'FULL_DOWN')
  assert.equal(storeCommandLabel('0x21'), 'FULL_UP')
  assert.equal(storeCommandLabel('0x41'), 'FULL_DOWN')
})

await test('contract preserves mode in save and backup, and rejects unknown modes', () => {
  const device = { device_type: 'cover', dst_address: address, src_address: '0x654321', movement_mode: 'step' }
  assert.equal(readCommand(JSON.stringify({ type: 'upsert_device', ...device })).type, 'upsert_device')
  assert.throws(() => readCommand(JSON.stringify({ type: 'upsert_device', ...device, movement_mode: 'unknown' })))
  const snapshot = { snapshot_version: 2, exported_at: 0, exporter: { device: 'test', version: 'test' }, hub: {}, groups: [], devices: [device] }
  assert.equal(readCommand(JSON.stringify({ type: 'import_config', snapshot })).type, 'import_config')
  assert.doesNotThrow(() => envelope('device_upserted', { address, device_type: 'cover', movement_mode: 'step' }))
})
