import {
  setConnected, setDevices, addRfPacket,
  onDeviceUpserted, onDeviceRemoved,
  devices,
} from './store'

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

export function initWs() {
  if (ws) {
    ws.onopen = ws.onclose = ws.onerror = ws.onmessage = null
    ws.close()
  }

  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  ws = new WebSocket(`${proto}//${location.host}/elero/ws`)
  const socket = ws

  socket.onopen = () => {
    setConnected(true)
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  socket.onclose = () => {
    setConnected(false)
    if (ws === socket) {
      ws = null
      reconnectTimer = setTimeout(initWs, 2000)
    }
  }

  socket.onerror = () => {
    setConnected(false)
  }

  socket.onmessage = (e) => {
    const { event, data } = JSON.parse(e.data)
    if (event === 'config') {
      setDevices(data)
    } else if (event === 'rf') {
      data.received_at = Date.now()
      addRfPacket(data)
    } else if (event === 'device_upserted') {
      onDeviceUpserted(data)
    } else if (event === 'device_removed') {
      onDeviceRemoved(data)
    }
  }
}

// ─── Action → Command byte mapping ─────────────────────────────────────────

const ACTION_COMMANDS: Record<string, string> = {
  up: '0x20',
  down: '0x40',
  stop: '0x10',
  tilt: '0x24',
  check: '0x00',
}

// ─── Unified command sending (always uses raw protocol) ─────────────────────

export interface RawTxParams {
  dst_address: string
  src_address: string
  channel: number
  command: string
  payload_1?: string
  payload_2?: string
  msg_type?: string
  type2?: string
  hop?: string
}

export function sendRawCommand(params: RawTxParams) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({
      type: 'raw',
      dst_address: params.dst_address,
      src_address: params.src_address,
      channel: params.channel,
      command: params.command,
      payload_1: params.payload_1 ?? '0x00',
      payload_2: params.payload_2 ?? '0x04',
      msg_type: params.msg_type ?? '0x6a',
      type2: params.type2 ?? '0x00',
      hop: params.hop ?? '0x0a',
    }))
  }
}

export function sendDeviceCommand(
  device: { address: string; remote: string; channel: number },
  action: 'up' | 'down' | 'stop' | 'tilt' | 'check',
) {
  const cmd = ACTION_COMMANDS[action]
  if (!cmd) return
  sendRawCommand({
    dst_address: device.address,
    src_address: device.remote,
    channel: device.channel,
    command: cmd,
  })
}

export function sendUpsertDevice(device: {
  address: string
  remote: string
  channel: number
  name?: string
  device_type?: 'cover' | 'light'
}) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({
      type: 'upsert_device',
      device_type: device.device_type ?? 'cover',
      dst_address: device.address,
      src_address: device.remote,
      channel: device.channel,
      name: device.name,
      enabled: true,
    }))
  }
}

export function sendCheckAll() {
  for (const d of devices.value.values()) {
    if (d.type === 'cover' || d.type === 'light') {
      sendDeviceCommand(d, 'check')
    }
  }
}
