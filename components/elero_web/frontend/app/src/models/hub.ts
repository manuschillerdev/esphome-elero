import { signal } from '@preact/signals'
import type { ConfigData, HubConfig, RadioConfig, FreqConfig, HubMode, BlindConfig, LightConfig, RemoteConfig, DeviceUpsertedData, CrudEventData } from '@/generated'
import type { RfPacketWithTimestamp } from '@/lib/protocol'

export interface ServerConfig {
  blinds: BlindConfig[]
  lights: LightConfig[]
  remotes: RemoteConfig[]
}

export function createHubModel() {
  const connected = signal(false)
  const hubState = signal<HubConfig>({
    device: '',
    version: '',
    mode: 'native',
    crud: false,
  })
  const radioState = signal<RadioConfig>({
    chipset: 'cc1101',
    rx_sensitivity: -104,
    freq: { freq0: '0x7a', freq1: '0x71', freq2: '0x21' },
  })
  const config = signal<ServerConfig>({ blinds: [], lights: [], remotes: [] })
  const packets = signal<RfPacketWithTimestamp[]>([])

  return {
    connected,
    hub: hubState,
    radio: radioState,
    config,
    packets,

    setConnected(val: boolean) {
      connected.value = val
    },

    setFromConfig(data: ConfigData) {
      hubState.value = data.hub
      radioState.value = data.radio
      config.value = {
        blinds: data.blinds ?? [],
        lights: data.lights ?? [],
        remotes: data.remotes ?? [],
      }
    },

    appendPacket(pkt: RfPacketWithTimestamp) {
      packets.value = [...packets.value, pkt]
    },

    clearPackets() {
      packets.value = []
    },

    applyUpserted(data: DeviceUpsertedData) {
      const prev = config.value
      if (data.device_type === 'cover') {
        const blind: BlindConfig = {
          address: data.address, name: data.name ?? '', channel: data.channel ?? 0,
          remote: data.remote ?? '', enabled: data.enabled ?? true,
          open_ms: data.open_ms ?? 0, close_ms: data.close_ms ?? 0,
          poll_ms: data.poll_ms ?? 0, supports_tilt: data.supports_tilt ?? false,
          updated_at: data.updated_at,
        }
        config.value = { ...prev, blinds: [...prev.blinds.filter(b => b.address !== data.address), blind] }
      } else if (data.device_type === 'light') {
        const light: LightConfig = {
          address: data.address, name: data.name ?? '', channel: data.channel ?? 0,
          remote: data.remote ?? '', enabled: data.enabled ?? true,
          dim_ms: data.dim_ms ?? 0, updated_at: data.updated_at,
        }
        config.value = { ...prev, lights: [...prev.lights.filter(l => l.address !== data.address), light] }
      } else if (data.device_type === 'remote') {
        const remote: RemoteConfig = {
          address: data.address, name: data.name ?? '', updated_at: data.updated_at,
        }
        config.value = { ...prev, remotes: [...prev.remotes.filter(r => r.address !== data.address), remote] }
      }
    },

    applyRemoved(data: CrudEventData) {
      const prev = config.value
      config.value = {
        blinds: prev.blinds.filter(b => b.address !== data.address),
        lights: prev.lights.filter(l => l.address !== data.address),
        remotes: prev.remotes.filter(r => r.address !== data.address),
      }
    },
  }
}

export type HubModel = ReturnType<typeof createHubModel>
