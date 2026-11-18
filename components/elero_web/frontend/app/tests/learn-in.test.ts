import assert from 'node:assert/strict'
import { beforeEach, test } from 'node:test'
import { addRfPacket, devices, learnInReceivers, onLearnInState, reviewLearnedCover } from '../src/store'
import type { LearnInState, RfData } from '../src/generated'

const remote = '0x0abcde'
const address = '0x123456'
const status: RfData = { t: 100, src: address, dst: remote, channel: 99, type: '0xca', type2: '0x00',
  command: '0x00', state: '0x01', cnt: 1, rssi: -60, hop: '0x00', raw: '' }

function transition(state: LearnInState) {
  onLearnInState({ state, active: state !== 'complete' && state !== 'idle',
    busy: state === 'programming', src_address: remote, channel: 3 })
}

beforeEach(() => {
  devices.value = new Map()
  transition('idle')
})

await test('only replies to this session suggest a receiver, including a late DOWN reply', (t) => {
  t.mock.timers.enable({ apis: ['Date'], now: 1000 })
  addRfPacket(status)
  transition('programming')
  addRfPacket({ ...status, dst: '0x112233' })
  addRfPacket({ ...status, state: '0x11' })
  transition('complete')
  addRfPacket(status)
  t.mock.timers.tick(10000)
  addRfPacket({ ...status, src: '0x111111' })
  assert.deepEqual(learnInReceivers.value, [address])
  assert.equal(devices.value.has(address), false)
  transition('programming')
  assert.deepEqual(learnInReceivers.value, [])
})

await test('explicit review creates one unsaved cover with the session remote and channel', () => {
  transition('programming')
  addRfPacket(status)
  assert.equal(reviewLearnedCover(address), false)
  transition('complete')
  assert.equal(reviewLearnedCover(remote), false)
  assert.equal(reviewLearnedCover('0x000000'), false)
  assert.equal(reviewLearnedCover(address), true)
  const cover = devices.value.get(address)!
  assert.equal(cover.remote, remote)
  assert.equal(cover.channel, 3)
  assert.equal(cover.updated_at, null)
  assert.equal(reviewLearnedCover(address), false)
  assert.equal(devices.value.get(address), cover)
})
