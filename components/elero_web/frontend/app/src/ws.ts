import { useStore } from './store'

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
      reconnectTimer = setTimeout(initWs, 2000)
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
      data.received_at = Date.now()
      state.addRfPacket(data)
    } else if (event === 'log') {
      state.addLog(data)
    }
  }
}

export function sendCommand(address: string, action: 'up' | 'down' | 'stop' | 'tilt' | 'check') {
  if (ws?.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ type: 'cmd', address, action }))
  }
}

export function sendCheckAll() {
  const { blinds, lights } = useStore.getState().config
  for (const blind of blinds) {
    sendCommand(blind.address, 'check')
  }
  for (const light of lights) {
    sendCommand(light.address, 'check')
  }
}

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
