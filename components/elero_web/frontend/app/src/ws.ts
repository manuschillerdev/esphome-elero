import type { CmdPayload, RawPayload, UpsertDevicePayload, RemoveDevicePayload, RestartPayload, SetHubConfigPayload, DeviceAction, StateChangedData, HubConfigEventData, ExportConfigPayload, ImportConfigPayload, ConfigSnapshot, ImportResult, LearnInStartPayload, LearnInConfirmUpPayload, LearnInConfirmDownPayload, LearnInCancelPayload, LearnInStateData, UpsertGroupPayload, RemoveGroupPayload, GroupCmdPayload, GroupConfig, GroupRemovedData, ErrorData } from '@/generated'
import {
  setConnected, setDevices, addRfPacket,
  onDeviceUpserted, onDeviceRemoved, onStateChanged, onHubConfig,
  onConfigSnapshot, onImportResult, onLearnInState, onGroupUpserted, onGroupRemoved,
  devices, showToast,
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
    } else if (event === 'group_upserted') {
      onGroupUpserted(data as GroupConfig)
    } else if (event === 'group_removed') {
      onGroupRemoved(data as GroupRemovedData)
    } else if (event === 'hub_config') {
      onHubConfig(data as HubConfigEventData)
    } else if (event === 'config_snapshot') {
      onConfigSnapshot(data as ConfigSnapshot)
    } else if (event === 'import_result') {
      onImportResult(data as ImportResult)
    } else if (event === 'learn_in_state') {
      onLearnInState(data as LearnInStateData)
    } else if (event === 'error') {
      showToast('error', (data as ErrorData).msg)
    }
  }
}

// ─── Send helpers ───────────────────────────────────────────────────────────

function send(payload: CmdPayload | RawPayload | UpsertDevicePayload | RemoveDevicePayload | RestartPayload | SetHubConfigPayload | ExportConfigPayload | ImportConfigPayload | LearnInStartPayload | LearnInConfirmUpPayload | LearnInConfirmDownPayload | LearnInCancelPayload | UpsertGroupPayload | RemoveGroupPayload | GroupCmdPayload) {
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

export function sendUpsertGroup(group: Omit<UpsertGroupPayload, 'type'>) {
  send({ type: 'upsert_group', ...group })
}

export function sendRemoveGroup(id: string) {
  send({ type: 'remove_group', id })
}

export function sendGroupCommand(id: string, action: DeviceAction) {
  send({ type: 'group_cmd', id, action })
}

export function sendRestart() {
  send({ type: 'restart' })
}

export function sendSetHubConfig(name: string) {
  send({ type: 'set_hub_config', name })
}

export function sendExportConfig() {
  send({ type: 'export_config' })
}

export function sendImportConfig(snapshot: ConfigSnapshot) {
  send({ type: 'import_config', snapshot })
}

export function sendCheckAll() {
  for (const d of devices.value.values()) {
    if (d.type === 'cover' || d.type === 'light') {
      sendDeviceCommand(d, 'check')
    }
  }
}

export function sendLearnInStart(params: Omit<LearnInStartPayload, 'type'>) {
  send({ type: 'learn_in_start', ...params })
}

export function sendLearnInConfirmUp() {
  send({ type: 'learn_in_confirm_up' })
}

export function sendLearnInConfirmDown() {
  send({ type: 'learn_in_confirm_down' })
}

export function sendLearnInCancel() {
  send({ type: 'learn_in_cancel' })
}
