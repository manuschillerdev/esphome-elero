import type { CmdPayload, RawPayload, UpsertDevicePayload, RemoveDevicePayload, RestartPayload, DeviceAction, StateChangedData } from '@/generated'
import {
  setConnected, setDevices, addRfPacket,
  onDeviceUpserted, onDeviceRemoved, onStateChanged,
  devices,
  type Device,
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
    } else if (event === 'state_changed') {
      onStateChanged(data as StateChangedData)
    } else if (event === 'device_upserted') {
      onDeviceUpserted(data)
    } else if (event === 'device_removed') {
      onDeviceRemoved(data)
    }
  }
}

// ─── Send helpers ───────────────────────────────────────────────────────────

function send(payload: CmdPayload | RawPayload | UpsertDevicePayload | RemoveDevicePayload | RestartPayload) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload))
  }
}

export function sendRawCommand(params: Omit<RawPayload, 'type'>) {
  send({ type: 'raw', ...params })
}

export function sendDeviceCommand(
  device: Pick<Device, 'address'>,
  action: DeviceAction,
) {
  send({ type: 'cmd', address: device.address, action })
}

export function sendUpsertDevice(device: Device) {
  const payload: UpsertDevicePayload = {
    type: 'upsert_device',
    device_type: device.type,
    dst_address: device.address,
    src_address: device.remote,
    channel: device.channel,
    name: device.name,
    enabled: device.enabled,
    open_duration_ms: device.open_ms,
    close_duration_ms: device.close_ms,
    supports_tilt: device.supports_tilt,
    dim_duration_ms: device.dim_ms,
  }
  send(payload)
}

export function sendRemoveDevice(address: string, device_type: RemoveDevicePayload['device_type']) {
  send({ type: 'remove_device', dst_address: address, device_type })
}

export function sendRestart() {
  send({ type: 'restart' })
}

export function sendCheckAll() {
  for (const d of devices.value.values()) {
    if (d.type === 'cover' || d.type === 'light') {
      sendDeviceCommand(d, 'check')
    }
  }
}
