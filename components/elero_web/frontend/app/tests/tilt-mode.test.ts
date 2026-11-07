import assert from 'node:assert/strict'
import { beforeEach, test } from 'node:test'
import { readCommand, envelope } from '../mock-contract'
import { devices, onDeviceUpserted, updateDevice } from '../src/store'

const address = '0x123456'
beforeEach(() => { devices.value = new Map() })

await test('old configurations keep preset tilt and the mode is independent of movement', () => {
  onDeviceUpserted({ address, device_type: 'cover', supports_tilt: true, movement_mode: 'step' })
  assert.equal(devices.value.get(address)?.tilt_mode, 'preset')
  updateDevice(address, { tilt_mode: 'step' })
  updateDevice(address, { movement_mode: 'standard' })
  assert.equal(devices.value.get(address)?.tilt_mode, 'step')
  assert.equal(devices.value.get(address)?.movement_mode, 'standard')
  assert.equal(devices.value.get(address)?.updated_at, null)
})

await test('server events retain selected tilt mode and custom saved-preset byte', () => {
  onDeviceUpserted({ address, device_type: 'cover', supports_tilt: true, tilt_mode: 'step', command_tilt: '0x25', updated_at: 1 })
  assert.equal(devices.value.get(address)?.tilt_mode, 'step')
  assert.equal(devices.value.get(address)?.command_tilt, '0x25')
  assert.equal(devices.value.get(address)?.updated_at, 1)
})

await test('save and backup contracts retain tilt mode without a snapshot version change', () => {
  for (const tilt_mode of ['preset', 'step'] as const) {
    const device = { device_type: 'cover' as const, dst_address: address, src_address: '0x654321', supports_tilt: true, tilt_mode }
    assert.equal(readCommand(JSON.stringify({ type: 'upsert_device', ...device })).type, 'upsert_device')
    const snapshot = { snapshot_version: 2, exported_at: 0, exporter: { device: 'test', version: 'test' }, hub: {}, groups: [], devices: [device] }
    assert.equal(readCommand(JSON.stringify({ type: 'import_config', snapshot })).type, 'import_config')
    assert.doesNotThrow(() => envelope('config_snapshot', snapshot))
    assert.doesNotThrow(() => envelope('device_upserted', { address, device_type: 'cover', tilt_mode }))
  }
})

await test('tilt mode contract rejects invalid values', () => {
  for (const tilt_mode of ['angle', '', 1, null]) {
    assert.throws(() => readCommand(JSON.stringify({ type: 'upsert_device', device_type: 'cover', dst_address: address, tilt_mode })))
  }
})
