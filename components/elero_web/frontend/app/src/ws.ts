import { useStore } from './store'

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

export function connect() {
  if (ws) {
    ws.onopen = ws.onclose = ws.onerror = ws.onmessage = null
    ws.close()
  }

  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  ws = new WebSocket(`${proto}//${location.host}/elero/ws`)
  const socket = ws

  socket.onopen = () => {
    useStore.getState().setConnected(true)
    if (reconnectTimer) {
      clearTimeout(reconnectTimer)
      reconnectTimer = null
    }
  }

  socket.onclose = () => {
    useStore.getState().setConnected(false)
    if (ws === socket) {
      ws = null
      reconnectTimer = setTimeout(connect, 2000)
    }
  }

  socket.onerror = () => {
    useStore.getState().setConnected(false)
  }

  socket.onmessage = (e) => {
    const { event, data } = JSON.parse(e.data)
    const state = useStore.getState()

    if (event === 'config') {
      state.setConfig(data)
    } else if (event === 'rf') {
      state.addRfPacket(data)
    } else if (event === 'log') {
      state.addLog(data)
    }
  }
}

export function sendCommand(address: string, action: 'up' | 'down' | 'stop' | 'tilt') {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'cmd', address, action }))
  }
}

export interface RawTxParams {
  blind_address: string
  remote_address: string
  channel: number
  command: string
  payload_1?: string
  payload_2?: string
  pck_inf1?: string
  pck_inf2?: string
  hop?: string
}

export function sendRawCommand(params: RawTxParams) {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({
      type: 'raw',
      blind_address: params.blind_address,
      remote_address: params.remote_address,
      channel: params.channel,
      command: params.command,
      payload_1: params.payload_1 ?? '0x00',
      payload_2: params.payload_2 ?? '0x04',
      pck_inf1: params.pck_inf1 ?? '0x6a',
      pck_inf2: params.pck_inf2 ?? '0x00',
      hop: params.hop ?? '0x0a',
    }))
  }
}

export function disconnect() {
  if (reconnectTimer) {
    clearTimeout(reconnectTimer)
    reconnectTimer = null
  }
  if (ws) {
    ws.onopen = ws.onclose = ws.onerror = ws.onmessage = null
    ws.close()
    ws = null
  }
}
