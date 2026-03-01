import { useMemo } from 'preact/hooks'
import { Card } from './ui/card'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Radio, Trash2, ChevronDown, Blinds, Lightbulb } from './icons'
import { useStore, deriveDeviceType, type DeviceType, type DeviceTypeFilter } from '@/store'
import { cn } from '@/lib/utils'

function formatTime(ms: number): string {
  if (!ms) return ''
  const s = Math.floor(ms / 1000)
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}

const deviceTypeLabels: Record<DeviceType, string> = {
  blind: 'Blind',
  light: 'Light',
  remote: 'Remote',
  unknown: '?',
}

const deviceTypeColors: Record<DeviceType, string> = {
  blind: 'bg-primary/10 text-primary',
  light: 'bg-warning/10 text-warning-foreground',
  remote: 'bg-muted text-muted-foreground',
  unknown: 'bg-muted text-muted-foreground',
}

export function RfPackets() {
  const rfPackets = useStore((s) => s.rfPackets)
  const rfFilter = useStore((s) => s.rfFilter)
  const deviceTypeFilter = useStore((s) => s.deviceTypeFilter)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)

  // Build address -> name lookup from config
  const configNames = useMemo(() => {
    const map: Record<string, string> = {}
    blinds.forEach((b) => { map[b.address] = b.name })
    lights.forEach((l) => { map[l.address] = l.name })
    return map
  }, [blinds, lights])

  // Build address -> derived type lookup
  const addressTypes = useMemo(() => {
    const map: Record<string, DeviceType> = {}
    const seen = new Set<string>()
    rfPackets.forEach((p) => {
      seen.add(p.src)
      seen.add(p.dst)
    })
    seen.forEach((addr) => {
      map[addr] = deriveDeviceType(rfPackets, addr)
    })
    return map
  }, [rfPackets])

  // Build unique addresses for dropdown, grouped by type
  const uniqueAddresses = useMemo(() => {
    const seen = new Set<string>()
    rfPackets.forEach((p) => {
      seen.add(p.src)
      seen.add(p.dst)
    })
    return Array.from(seen).sort((a, b) => {
      // Sort by: configured first, then by type, then by address
      const aConfigured = !!configNames[a]
      const bConfigured = !!configNames[b]
      if (aConfigured && !bConfigured) return -1
      if (!aConfigured && bConfigured) return 1
      const aType = addressTypes[a] ?? 'unknown'
      const bType = addressTypes[b] ?? 'unknown'
      if (aType !== bType) return aType.localeCompare(bType)
      return a.localeCompare(b)
    })
  }, [rfPackets, configNames, addressTypes])

  const filtered = useMemo(() => {
    let pkts = rfPackets

    // Filter by address
    if (rfFilter) {
      pkts = pkts.filter((p) => p.src === rfFilter || p.dst === rfFilter)
    }

    // Filter by device type
    if (deviceTypeFilter !== 'all') {
      pkts = pkts.filter((p) => {
        const srcType = addressTypes[p.src]
        const dstType = addressTypes[p.dst]
        if (deviceTypeFilter === 'blinds') {
          return srcType === 'blind' || dstType === 'blind'
        }
        if (deviceTypeFilter === 'lights') {
          return srcType === 'light' || dstType === 'light'
        }
        return true
      })
    }

    return pkts.slice(-50).reverse()
  }, [rfPackets, rfFilter, deviceTypeFilter, addressTypes])

  const renderAddress = (addr: string) => {
    const name = configNames[addr]
    const deviceType = addressTypes[addr] ?? 'unknown'

    return (
      <span className="flex items-center gap-2">
        <span className="flex flex-col">
          {name ? (
            <>
              <span className="text-foreground">{name}</span>
              <span className="text-[10px] text-muted-foreground">{addr}</span>
            </>
          ) : (
            <span>{addr}</span>
          )}
        </span>
        {deviceType !== 'unknown' && (
          <Badge variant="secondary" className={cn('text-[9px] px-1.5 py-0', deviceTypeColors[deviceType])}>
            {deviceTypeLabels[deviceType]}
          </Badge>
        )}
      </span>
    )
  }

  const formatDropdownLabel = (addr: string) => {
    const name = configNames[addr]
    const deviceType = addressTypes[addr] ?? 'unknown'
    const typeLabel = deviceType !== 'unknown' ? ` [${deviceTypeLabels[deviceType]}]` : ''
    return name ? `${name} (${addr})${typeLabel}` : `${addr}${typeLabel}`
  }

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex items-center justify-between border-b border-border px-5 py-4">
        <div className="flex items-center gap-3">
          <div className="flex size-8 items-center justify-center rounded-lg bg-primary/10 text-primary">
            <Radio className="size-4" />
          </div>
          <div>
            <h2 className="text-sm font-semibold text-card-foreground">RF Packets</h2>
            <p className="text-xs text-muted-foreground">{rfPackets.length} packets captured</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          {/* Device type filter */}
          <div className="flex items-center gap-1 rounded-lg bg-muted p-1">
            {([
              { value: 'all', label: 'All' },
              { value: 'blinds', label: 'Blinds', icon: Blinds },
              { value: 'lights', label: 'Lights', icon: Lightbulb },
            ] as { value: DeviceTypeFilter; label: string; icon?: typeof Blinds }[]).map((dt) => {
              const Icon = dt.icon
              return (
                <button
                  key={dt.value}
                  onClick={() => useStore.getState().setDeviceTypeFilter(dt.value)}
                  className={cn(
                    'flex items-center gap-1 rounded-md px-2 py-1 text-xs font-medium transition-colors',
                    deviceTypeFilter === dt.value
                      ? 'bg-card text-card-foreground shadow-sm'
                      : 'text-muted-foreground hover:text-foreground'
                  )}
                >
                  {Icon ? <Icon className="size-3" /> : dt.label}
                </button>
              )
            })}
          </div>

          {/* Address filter */}
          <div className="relative">
            <select
              value={rfFilter}
              onChange={(e) => useStore.getState().setRfFilter((e.target as HTMLSelectElement).value)}
              className={cn(
                'h-8 w-48 appearance-none rounded-md border border-input bg-background px-3 pr-8 text-xs',
                'focus:outline-none focus:ring-2 focus:ring-ring',
                !rfFilter && 'text-muted-foreground'
              )}
            >
              <option value="">All addresses</option>
              {uniqueAddresses.map((addr) => (
                <option key={addr} value={addr}>
                  {formatDropdownLabel(addr)}
                </option>
              ))}
            </select>
            <ChevronDown className="pointer-events-none absolute right-2 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
          </div>

          <Button
            variant="outline"
            size="sm"
            className="gap-1.5 text-xs"
            onClick={() => useStore.getState().clearRfPackets()}
          >
            <Trash2 className="size-3" />
            Clear
          </Button>
        </div>
      </div>

      <div className="max-h-[500px] overflow-y-auto">
        <table className="w-full text-xs font-mono">
          <thead className="bg-muted/50 sticky top-0">
            <tr>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">Time</th>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">Source</th>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">Destination</th>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">CH</th>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">Type</th>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">Cmd/State</th>
              <th className="px-3 py-2 text-left font-medium text-muted-foreground">RSSI</th>
            </tr>
          </thead>
          <tbody>
            {filtered.map((pkt, i) => (
              <tr key={`${pkt.t}-${i}`} className="border-t border-border hover:bg-muted/30">
                <td className="px-3 py-2 text-muted-foreground">{formatTime(pkt.t)}</td>
                <td className="px-3 py-2">{renderAddress(pkt.src)}</td>
                <td className="px-3 py-2">{renderAddress(pkt.dst)}</td>
                <td className="px-3 py-2 text-muted-foreground">{pkt.ch ?? '-'}</td>
                <td className="px-3 py-2">
                  <Badge variant="secondary" className="text-[10px] px-1.5 py-0">
                    {pkt.type === '0x44' ? 'BTN' : pkt.type === '0x6a' ? 'CMD' : pkt.type === '0xca' || pkt.type === '0xc9' ? 'STS' : pkt.type}
                  </Badge>
                </td>
                <td className="px-3 py-2">{pkt.type === '0x6a' ? pkt.cmd : pkt.state}</td>
                <td className="px-3 py-2 text-muted-foreground">{pkt.rssi?.toFixed(1) ?? '-'}</td>
              </tr>
            ))}
          </tbody>
        </table>
        {filtered.length === 0 && (
          <div className="p-8 text-center text-sm text-muted-foreground">No packets</div>
        )}
      </div>
    </Card>
  )
}
