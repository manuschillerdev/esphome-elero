import assert from 'node:assert/strict'
import { test } from 'node:test'
import { addRfPacket, devices } from '../src/store'
import type { RfData } from '../src/generated'

// The two-receiver CHECK in issue #70 uses type 0x6a. Type 0x69 has the same address layout.
for (const type of ['0x6a', '0x69']) {
  await test(`discovery associates both receivers in a ${type} CHECK with the same remote channel`, () => {
    const remote = '0xaaaaaa'
    const destinations = ['0x111111', '0x222222']
    const packet: RfData = {
      t: 0, src: remote, dst: destinations[0], destinations, channel: 69, type, type2: '0x10',
      cnt: 1, command: '0x00', state: '0x00', hop: '0x00', rssi: -50, raw: '',
    }
    devices.value = new Map()
    addRfPacket(packet)

    for (const address of destinations) {
      const cover = devices.value.get(address)
      assert.equal(cover?.type, 'cover', address)
      assert.equal(cover.remote, remote)
      assert.equal(cover.channel, 69)
      assert.deepEqual(cover.pairings, [{ remote, channel: 69 }])
    }
  })
}
