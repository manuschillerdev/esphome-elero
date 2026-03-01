import { WebSocketServer, WebSocket } from 'ws'

const PORT = 8080

// ─── Mock Data ───────────────────────────────────────────────────────────────

const CONFIG = {
  device: 'lilygo-t-embed',
  blinds: [
    { address: '0x313238', name: 'Terrasse', channel: 2, remote: '0x17a753', open_ms: 25000, close_ms: 25000, poll_ms: 300000, tilt: false },
    { address: '0x333238', name: 'Lichthof OG', channel: 7, remote: '0x17a753', open_ms: 20000, close_ms: 20000, poll_ms: 300000, tilt: true },
    { address: '0x413238', name: 'Terrasse Küche', channel: 3, remote: '0x28b864', open_ms: 30000, close_ms: 30000, poll_ms: 300000, tilt: false },
    { address: '0x4d5748', name: 'Schlafzimmer', channel: 33, remote: '0x28b864', open_ms: 22000, close_ms: 22000, poll_ms: 300000, tilt: true },
  ],
  lights: [
    { address: '0xc41a2b', name: 'Gartenleuchte', channel: 5, remote: '0x17a753', dim_ms: 0 },
    { address: '0xd52b3c', name: 'Terrassenspot', channel: 8, remote: '0x28b864', dim_ms: 5000 },
  ],
  freq: { freq0: '0x7a', freq1: '0x71', freq2: '0x21' },
}

// Known remotes (will send 0x44 button packets)
const REMOTES = [
  { address: '0x17a753', name: 'Fernbedienung 1' },
  { address: '0x28b864', name: 'Fernbedienung 2' },
]

// Blind state hex codes (matching C++ ELERO_STATE_* constants)
const STATE_HEX = {
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
  ON: '0x10',
  OFF: '0x0f', // OFF shares value with BOTTOM_TILT in protocol
} as const

// Blind states for random selection
const BLIND_STATES = ['TOP', 'BOTTOM', 'INTERMEDIATE', 'MOVING_UP', 'MOVING_DOWN', 'STOPPED', 'TILT', 'BLOCKING'] as const
// Light states for random selection
const LIGHT_STATES = ['ON', 'OFF'] as const
// Commands
const COMMANDS = ['CHECK', 'STOP', 'UP', 'DOWN', 'TILT']

const LOG_TAGS = ['elero', 'cc1101', 'wifi', 'ota', 'api']
const LOG_MESSAGES = [
  'Received packet from blind',
  'Sending command',
  'RSSI: -75 dBm',
  'State changed',
  'Polling blind status',
  'Connection established',
  'Frequency set to 868.35 MHz',
]

let startTime = Date.now()
let packetCounter = 0

// ─── Helpers ─────────────────────────────────────────────────────────────────

function randomFrom<T>(arr: T[]): T {
  return arr[Math.floor(Math.random() * arr.length)]
}

// Generate realistic RF packet based on protocol spec
// Only emits packets from configured devices (no random addresses)
function generateRfPacket() {
  const rand = Math.random()

  // 45% - Status response from configured blind (0xca/0xc9)
  if (rand < 0.45) {
    const blind = randomFrom(CONFIG.blinds)
    const stateKey = randomFrom(BLIND_STATES)
    return {
      t: Date.now() - startTime,
      src: blind.address,
      dst: blind.remote,
      type: Math.random() > 0.5 ? '0xca' : '0xc9',
      state: STATE_HEX[stateKey],
      rssi: -60 - Math.random() * 30,
      ch: blind.channel,
    }
  }

  // 20% - Status response from light (0xca with ON/OFF)
  if (rand < 0.65) {
    const light = randomFrom(CONFIG.lights)
    const stateKey = randomFrom(LIGHT_STATES)
    return {
      t: Date.now() - startTime,
      src: light.address,
      dst: light.remote,
      type: '0xca',
      state: STATE_HEX[stateKey],
      rssi: -65 - Math.random() * 25,
      ch: light.channel,
    }
  }

  // 20% - Button press from remote (0x44)
  if (rand < 0.85) {
    const remote = randomFrom(REMOTES)
    const allDevices = [...CONFIG.blinds, ...CONFIG.lights]
    const target = randomFrom(allDevices)
    const isLight = CONFIG.lights.some(l => l.address === target.address)
    const cmd = isLight
      ? randomFrom(['0x20', '0x40']) // ON=UP, OFF=DOWN
      : randomFrom(['0x00', '0x10', '0x20', '0x40', '0x24']) // CHECK, STOP, UP, DOWN, TILT
    return {
      t: Date.now() - startTime,
      src: remote.address,
      dst: target.address,
      type: '0x44',
      cmd,
      rssi: -55 - Math.random() * 20,
      ch: target.channel,
    }
  }

  // 15% - Command from controller (0x6a) - this is what our ESP sends
  const allDevices = [...CONFIG.blinds, ...CONFIG.lights]
  const target = randomFrom(allDevices)
  const isLight = CONFIG.lights.some(l => l.address === target.address)
  const cmd = isLight
    ? randomFrom(['0x20', '0x40']) // ON=UP, OFF=DOWN
    : randomFrom(['0x00', '0x10', '0x20', '0x40', '0x24']) // CHECK, STOP, UP, DOWN, TILT
  return {
    t: Date.now() - startTime,
    src: '0x000001', // Controller address
    dst: target.address,
    type: '0x6a',
    cmd,
    rssi: -50 - Math.random() * 15,
    ch: target.channel,
  }
}

function generateLog() {
  return {
    t: Date.now() - startTime,
    level: Math.random() > 0.9 ? 2 : Math.random() > 0.95 ? 1 : 3,
    tag: randomFrom(LOG_TAGS),
    msg: randomFrom(LOG_MESSAGES),
  }
}

// ─── Server ──────────────────────────────────────────────────────────────────

const wss = new WebSocketServer({ port: PORT })

console.log(`Mock WebSocket server running on ws://localhost:${PORT}`)
console.log(`\nConfigure vite proxy to forward /elero/ws to this server`)
console.log(`\nPress Ctrl+C to stop\n`)

wss.on('connection', (ws: WebSocket) => {
  console.log('Client connected')
  startTime = Date.now()

  // Send config immediately
  ws.send(JSON.stringify({ event: 'config', data: CONFIG }))
  console.log('→ Sent config')

  // Send initial states for configured blinds (status responses)
  CONFIG.blinds.forEach((blind, i) => {
    setTimeout(() => {
      const stateKey = randomFrom(BLIND_STATES)
      const pkt = {
        t: Date.now() - startTime,
        src: blind.address,
        dst: blind.remote,
        type: '0xca',
        state: STATE_HEX[stateKey],
        rssi: -65 - Math.random() * 20,
        ch: blind.channel,
      }
      ws.send(JSON.stringify({ event: 'rf', data: pkt }))
    }, 100 * (i + 1))
  })

  // Send initial state for lights
  CONFIG.lights.forEach((light, i) => {
    setTimeout(() => {
      const stateKey = randomFrom(LIGHT_STATES)
      const pkt = {
        t: Date.now() - startTime,
        src: light.address,
        dst: light.remote,
        type: '0xca',
        state: STATE_HEX[stateKey],
        rssi: -70 - Math.random() * 15,
        ch: light.channel,
      }
      ws.send(JSON.stringify({ event: 'rf', data: pkt }))
    }, 100 * (CONFIG.blinds.length + i + 1))
  })

  // Periodic RF packets (every 2-5 seconds)
  const rfInterval = setInterval(() => {
    if (ws.readyState !== WebSocket.OPEN) return
    const pkt = generateRfPacket()
    ws.send(JSON.stringify({ event: 'rf', data: pkt }))
    packetCounter++
    const typeLabel = pkt.type === '0x44' ? 'BTN' : pkt.type === '0x6a' ? 'CMD' : 'STS'
    console.log(`→ RF #${packetCounter} [${typeLabel}] ${pkt.src} → ${pkt.dst}`)
  }, 2000 + Math.random() * 3000)

  // Periodic logs (every 3-8 seconds)
  const logInterval = setInterval(() => {
    if (ws.readyState !== WebSocket.OPEN) return
    const log = generateLog()
    ws.send(JSON.stringify({ event: 'log', data: log }))
    console.log(`→ Log [${log.tag}] ${log.msg}`)
  }, 3000 + Math.random() * 5000)

  // Handle commands
  ws.on('message', (data: Buffer) => {
    try {
      const msg = JSON.parse(data.toString())
      console.log(`← Command: ${msg.type} ${msg.address} ${msg.action}`)

      if (msg.type === 'cmd') {
        // Check if target is a light or blind
        const isLight = CONFIG.lights.some(l => l.address === msg.address)
        const blind = CONFIG.blinds.find(b => b.address === msg.address)
        const light = CONFIG.lights.find(l => l.address === msg.address)
        const remote = blind?.remote ?? light?.remote ?? REMOTES[0].address

        // Simulate state change after command
        setTimeout(() => {
          let newStateKey: keyof typeof STATE_HEX
          let ch: number

          if (isLight) {
            // Light responds with ON/OFF
            newStateKey = msg.action === 'up' || msg.action === 'on' ? 'ON' : 'OFF'
            ch = light?.channel ?? 0
          } else {
            // Blind responds with movement state
            newStateKey = msg.action === 'up' ? 'MOVING_UP'
              : msg.action === 'down' ? 'MOVING_DOWN'
              : msg.action === 'stop' ? 'STOPPED'
              : msg.action === 'tilt' ? 'TILT'
              : 'INTERMEDIATE'
            ch = blind?.channel ?? 0
          }

          const pkt = {
            t: Date.now() - startTime,
            src: msg.address,
            dst: remote,
            type: '0xca',
            state: STATE_HEX[newStateKey],
            rssi: -70 - Math.random() * 15,
            ch,
          }
          ws.send(JSON.stringify({ event: 'rf', data: pkt }))
          console.log(`→ State response: ${msg.address} → ${newStateKey}`)

          // For blinds, send final position after movement
          if (!isLight && (newStateKey === 'MOVING_UP' || newStateKey === 'MOVING_DOWN')) {
            setTimeout(() => {
              const finalStateKey = newStateKey === 'MOVING_UP' ? 'TOP' : 'BOTTOM'
              const finalPkt = {
                t: Date.now() - startTime,
                src: msg.address,
                dst: remote,
                type: '0xca',
                state: STATE_HEX[finalStateKey],
                rssi: -70 - Math.random() * 15,
                ch,
              }
              ws.send(JSON.stringify({ event: 'rf', data: finalPkt }))
              console.log(`→ Final state: ${msg.address} → ${finalStateKey}`)
            }, 2000 + Math.random() * 1000)
          }
        }, 300 + Math.random() * 200)
      }
    } catch (e) {
      console.error('Invalid message:', data.toString())
    }
  })

  ws.on('close', () => {
    console.log('Client disconnected')
    clearInterval(rfInterval)
    clearInterval(logInterval)
  })
})
