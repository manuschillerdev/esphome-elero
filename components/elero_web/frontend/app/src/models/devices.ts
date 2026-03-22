import { signal, computed } from '@preact/signals'
import type { DeviceType, BlindConfig, LightConfig, RemoteConfig } from '@/generated'
import { msg_type, resolveStateName, DISCOVERY_COMMANDS } from '@/lib/protocol'
import type { RfPacketWithTimestamp } from '@/lib/protocol'
import type { UiModel } from './ui'
import type { HubModel } from './hub'

// ─── Discriminated Union ────────────────────────────────────────────────────

interface DeviceBase {
  address: string
  name: string
  updated_at: number | null
}

export interface Cover extends DeviceBase {
  type: 'cover'
  enabled: boolean
  channel: number
  remote: string
  open_ms: number
  close_ms: number
  poll_ms: number
  supports_tilt: boolean
}

export interface Light extends DeviceBase {
  type: 'light'
  enabled: boolean
  channel: number
  remote: string
  dim_ms: number
}

export interface Remote extends DeviceBase {
  type: 'remote'
}

export type Device = Cover | Light | Remote

export interface DeviceUpdates {
  name?: string
  enabled?: boolean
  channel?: number
  remote?: string
  open_ms?: number
  close_ms?: number
  poll_ms?: number
  supports_tilt?: boolean
  dim_ms?: number
}

export interface DeviceGroup {
  remote: Remote
  devices: (Cover | Light)[]
}

// ─── Config → Device converters ─────────────────────────────────────────────

function configToCover(b: BlindConfig): Cover {
  return {
    type: 'cover', address: b.address, name: b.name, updated_at: b.updated_at ?? null,
    enabled: b.enabled, channel: b.channel, remote: b.remote,
    open_ms: b.open_ms, close_ms: b.close_ms, poll_ms: b.poll_ms, supports_tilt: b.supports_tilt,
  }
}

function configToLight(l: LightConfig): Light {
  return {
    type: 'light', address: l.address, name: l.name, updated_at: l.updated_at ?? null,
    enabled: l.enabled, channel: l.channel, remote: l.remote, dim_ms: l.dim_ms,
  }
}

function configToRemote(r: RemoteConfig): Remote {
  return { type: 'remote', address: r.address, name: r.name, updated_at: r.updated_at ?? null }
}

// ─── Factories for RF-discovered devices ────────────────────────────────────

function makeCover(partial: Partial<Cover> & Pick<Cover, 'address'>): Cover {
  return {
    type: 'cover', name: '', updated_at: null, enabled: true, channel: 0, remote: '',
    open_ms: 0, close_ms: 0, poll_ms: 0, supports_tilt: false,
    ...partial,
  }
}

function makeRemote(address: string, name = ''): Remote {
  return { type: 'remote', address, name, updated_at: null }
}

function coverToLight(cover: Cover): Light {
  return {
    type: 'light', address: cover.address, name: cover.name, updated_at: cover.updated_at,
    enabled: cover.enabled, channel: cover.channel, remote: cover.remote, dim_ms: 0,
  }
}

// ─── YAML Generation ────────────────────────────────────────────────────────

function formatDuration(ms: number): string {
  if (ms >= 60000 && ms % 60000 === 0) return `${ms / 60000}min`
  if (ms >= 1000 && ms % 1000 === 0) return `${ms / 1000}s`
  return `${ms}ms`
}

function coverToYaml(d: Cover): string {
  return [
    '  - platform: elero',
    ...(d.name ? [`    name: "${d.name}"`] : []),
    `    dst_address: ${d.address}`,
    `    src_address: ${d.remote}`,
    `    channel: ${d.channel}`,
    ...(d.open_ms > 0 ? [`    open_duration: ${formatDuration(d.open_ms)}`] : []),
    ...(d.close_ms > 0 ? [`    close_duration: ${formatDuration(d.close_ms)}`] : []),
    ...(d.supports_tilt ? ['    supports_tilt: true'] : []),
    ...(d.poll_ms > 0 ? [`    poll_interval: ${formatDuration(d.poll_ms)}`] : []),
  ].join('\n')
}

function lightToYaml(d: Light): string {
  return [
    '  - platform: elero',
    ...(d.name ? [`    name: "${d.name}"`] : []),
    `    dst_address: ${d.address}`,
    `    src_address: ${d.remote}`,
    `    channel: ${d.channel}`,
    ...(d.dim_ms > 0 ? [`    dim_duration: ${formatDuration(d.dim_ms)}`] : []),
  ].join('\n')
}

// ─── Device Model ───────────────────────────────────────────────────────────

export function createDeviceModel(uiModel: UiModel, hubModel: HubModel) {
  // User edits only — not derived from config. Cleared on CRUD confirm.
  const base = signal<Map<string, Device>>(new Map())

  // ─── RF-derived: discovery + type corrections (from hub.packets) ──────

  const rfDerived = computed(() => {
    const discovered = new Map<string, Device>()
    const typeCorrections = new Map<string, 'light'>()

    for (const pkt of hubModel.packets.value) {
      const t = pkt.type?.toLowerCase()

      if ((t === msg_type.COMMAND || t === msg_type.COMMAND_ALT) && !pkt.echo) {
        const cmd = pkt.command?.toLowerCase()
        if (cmd && DISCOVERY_COMMANDS.has(cmd)) {
          if (!discovered.has(pkt.dst)) {
            discovered.set(pkt.dst, makeCover({ address: pkt.dst, remote: pkt.src, channel: pkt.channel }))
          }
          if (!discovered.has(pkt.src)) {
            discovered.set(pkt.src, makeRemote(pkt.src))
          }
        }
      } else if (t === msg_type.STATUS || t === msg_type.STATUS_ALT) {
        if (resolveStateName(pkt.state) === 'light_on') {
          typeCorrections.set(pkt.src, 'light')
        }
      } else if (t === msg_type.BUTTON) {
        if (!discovered.has(pkt.src)) {
          discovered.set(pkt.src, makeRemote(pkt.src))
        }
      }
    }

    return { discovered, typeCorrections }
  })

  // ─── RF-derived: latest status per address (from hub.packets) ─────────

  const statusOf = computed<Map<string, RfPacketWithTimestamp>>(() => {
    const result = new Map<string, RfPacketWithTimestamp>()
    for (const pkt of hubModel.packets.value) {
      const t = pkt.type?.toLowerCase()
      if (t === msg_type.STATUS || t === msg_type.STATUS_ALT) {
        result.set(pkt.src, pkt)
      }
    }
    return result
  })

  // ─── Merged view: RF discovered → hub.config → base → type corrections

  const all = computed<Map<string, Device>>(() => {
    const { discovered, typeCorrections } = rfDerived.value
    const hubConfig = hubModel.config.value
    const merged = new Map<string, Device>()

    // 1. RF-discovered (lowest priority)
    for (const [addr, dev] of discovered) {
      merged.set(addr, dev)
    }

    // 2. Hub config (overrides RF discovery)
    for (const b of hubConfig.blinds) merged.set(b.address, configToCover(b))
    for (const l of hubConfig.lights) merged.set(l.address, configToLight(l))
    for (const r of hubConfig.remotes) merged.set(r.address, configToRemote(r))
    // Ensure remotes exist for all covers/lights
    for (const b of hubConfig.blinds) {
      if (!merged.has(b.remote)) merged.set(b.remote, makeRemote(b.remote))
    }
    for (const l of hubConfig.lights) {
      if (!merged.has(l.remote)) merged.set(l.remote, makeRemote(l.remote))
    }

    // 3. Base overrides (highest priority — user edits)
    for (const [addr, dev] of base.value) {
      merged.set(addr, dev)
    }

    // 4. Type corrections (cover → light from RF LIGHT_ON observation)
    for (const [addr, correctedType] of typeCorrections) {
      const dev = merged.get(addr)
      if (dev && dev.type === 'cover' && correctedType === 'light') {
        merged.set(addr, coverToLight(dev))
      }
    }

    return merged
  })

  // ─── Filtered/derived computeds ───────────────────────────────────────

  const groups = computed<DeviceGroup[]>(() => {
    const devs = all.value
    const { status, deviceType } = uiModel.state.value.filters
    const groupMap = new Map<string, (Cover | Light)[]>()

    for (const d of devs.values()) {
      if (d.type === 'remote') continue
      if (status === 'saved' && d.updated_at === null) continue
      if (status === 'unsaved' && d.updated_at !== null) continue
      if (deviceType === 'covers' && d.type !== 'cover') continue
      if (deviceType === 'lights' && d.type !== 'light') continue
      const key = d.remote || 'unknown'
      const arr = groupMap.get(key)
      if (arr) arr.push(d)
      else groupMap.set(key, [d])
    }

    return [...groupMap]
      .sort(([a], [b]) => a.localeCompare(b))
      .map(([addr, items]) => {
        const existing = devs.get(addr)
        const remote: Remote = existing?.type === 'remote'
          ? existing
          : makeRemote(addr)
        return {
          remote,
          devices: items.sort((a, b) => a.address.localeCompare(b.address)),
        }
      })
  })

  const counts = computed(() => {
    let saved = 0, unsaved = 0, covers = 0, lights = 0
    for (const d of all.value.values()) {
      if (d.type === 'remote') continue
      if (d.updated_at !== null) saved++; else unsaved++
      if (d.type === 'cover') covers++
      if (d.type === 'light') lights++
    }
    return { all: saved + unsaved, saved, unsaved, covers, lights }
  })

  const names = computed<Record<string, string>>(() => {
    const result: Record<string, string> = {}
    for (const [addr, d] of all.value) result[addr] = d.name || addr
    return result
  })

  const types = computed<Record<string, DeviceType>>(() => {
    const result: Record<string, DeviceType> = {}
    for (const [addr, d] of all.value) result[addr] = d.type
    return result
  })

  // ─── Actions ──────────────────────────────────────────────────────────

  return {
    all,
    statusOf,
    groups,
    counts,
    names,
    types,

    /** Write a user edit to base. Promotes RF-discovered devices on first edit. */
    update(address: string, updates: DeviceUpdates) {
      const d = all.value.get(address)
      if (!d) return
      const next = new Map(base.value)
      next.set(address, { ...d, ...updates, updated_at: null } as Device)
      base.value = next
    },

    /** Remove an address from base after a CRUD confirm (upserted or removed). */
    clearEdit(address: string) {
      if (!base.value.has(address)) return
      const next = new Map(base.value)
      next.delete(address)
      base.value = next
    },

    /** Generate ESPHome YAML for all covers and lights. */
    exportYaml(): string {
      const devs = [...all.value.values()]
      const coverDevs = devs.filter((d): d is Cover => d.type === 'cover')
      const lightDevs = devs.filter((d): d is Light => d.type === 'light')
      const sections: string[] = []

      if (coverDevs.length > 0) {
        sections.push(['cover:', ...coverDevs.map(coverToYaml)].join('\n'))
      }
      if (lightDevs.length > 0) {
        sections.push(['light:', ...lightDevs.map(lightToYaml)].join('\n'))
      }

      return sections.join('\n\n') + '\n'
    },
  }
}

export type DeviceModel = ReturnType<typeof createDeviceModel>
