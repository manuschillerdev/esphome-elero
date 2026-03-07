import { useSignal } from '@preact/signals'
import { Card } from './ui/card'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { FilterBar, type FilterOption } from './ui/filter-bar'
import { DataTable, type Column } from './ui/data-table'
import { Blinds, Lightbulb, Copy, CheckCircle2, RemoteControl, Trash2 } from './icons'
import {
  useStore, buildConfigNames, buildAddressTypes,
  isStatusPacket, isCommandPacket, isButtonPacket,
  getMsgTypeLabel, getCommandLabel, getStateLabel,
  type RfPacket, type DeviceType, type DeviceTypeFilter,
} from '@/store'
import { cn } from '@/lib/utils'

// ─── Helpers ────────────────────────────────────────────────────────────────

function formatTime(epochMs: number | undefined): string {
  if (!epochMs) return ''
  const d = new Date(epochMs)
  return `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}:${String(d.getSeconds()).padStart(2, '0')}`
}

const deviceTypeIcons: Record<DeviceType, typeof Blinds | null> = {
  blind: Blinds,
  light: Lightbulb,
  remote: RemoteControl,
  unknown: null,
}

// ─── Copy Button ────────────────────────────────────────────────────────────

function CopyPacketBtn({ pkt }: { pkt: RfPacket }) {
  const copied = useSignal(false)

  const onClick = () => {
    const { received_at: _, ...rest } = pkt as Record<string, unknown>
    navigator.clipboard.writeText(JSON.stringify(rest, null, 2))
    copied.value = true
    setTimeout(() => { copied.value = false }, 1500)
  }

  return (
    <Tooltip>
      <TooltipTrigger>
        <button
          className={cn(
            'flex size-6 items-center justify-center rounded transition-colors',
            copied.value ? 'text-success' : 'text-muted-foreground hover:text-foreground hover:bg-muted'
          )}
          onClick={onClick}
        >
          {copied.value ? <CheckCircle2 className="size-3" /> : <Copy className="size-3" />}
        </button>
      </TooltipTrigger>
      <TooltipContent className="right-0 left-auto translate-x-0">Copy packet JSON</TooltipContent>
    </Tooltip>
  )
}

// ─── Address Rendering ──────────────────────────────────────────────────────

function AddressCell({ addr, name, deviceType }: { addr: string; name?: string; deviceType: DeviceType }) {
  const Icon = deviceTypeIcons[deviceType]
  return (
    <span className="flex items-center gap-2">
      <span className="flex flex-col">
        {name ? (
          <>
            <span className="text-foreground">{name}</span>
            <span className="flex items-center gap-1 text-[10px] text-muted-foreground">
              {Icon && <Icon className="size-2.5" />}
              {addr}
            </span>
          </>
        ) : (
          <span className="flex items-center gap-1">
            {Icon && <Icon className="size-3" />}
            {addr}
          </span>
        )}
      </span>
    </span>
  )
}

// ─── Main Component ─────────────────────────────────────────────────────────

export function RfPackets() {
  const rfPackets = useStore((s) => s.rfPackets)
  const rfFilter = useStore((s) => s.rfFilter)
  const deviceTypeFilter = useStore((s) => s.deviceTypeFilter)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)
  const remoteNames = useStore((s) => s.remoteNames)

  // Derive during render — cheap, only runs when store subscriptions change
  const configNames = buildConfigNames(blinds, lights, remoteNames)
  const addressTypes = buildAddressTypes(blinds, lights, rfPackets)

  const uniqueAddresses = (() => {
    const seen = new Set<string>()
    rfPackets.forEach((p) => { seen.add(p.src); seen.add(p.dst) })
    return Array.from(seen).sort((a, b) => {
      const aConfigured = !!configNames[a]
      const bConfigured = !!configNames[b]
      if (aConfigured && !bConfigured) return -1
      if (!aConfigured && bConfigured) return 1
      return a.localeCompare(b)
    })
  })()

  const typeCounts = (() => {
    let blindCount = 0
    let lightCount = 0
    rfPackets.forEach((p) => {
      const srcType = addressTypes[p.src]
      const dstType = addressTypes[p.dst]
      if (srcType === 'blind' || dstType === 'blind') blindCount++
      if (srcType === 'light' || dstType === 'light') lightCount++
    })
    return { all: rfPackets.length, blinds: blindCount, lights: lightCount }
  })()

  const filtered = (() => {
    let pkts = rfPackets
    if (rfFilter) pkts = pkts.filter((p) => p.src === rfFilter || p.dst === rfFilter)
    if (deviceTypeFilter !== 'all') {
      pkts = pkts.filter((p) => {
        const srcType = addressTypes[p.src]
        const dstType = addressTypes[p.dst]
        if (deviceTypeFilter === 'blinds') return srcType === 'blind' || dstType === 'blind'
        if (deviceTypeFilter === 'lights') return srcType === 'light' || dstType === 'light'
        return true
      })
    }
    return pkts.slice(-50).reverse()
  })()

  const formatDropdownLabel = (addr: string) => {
    const name = configNames[addr]
    const deviceType = addressTypes[addr] ?? 'unknown'
    const typeLabel = deviceType !== 'unknown' ? ` [${deviceType}]` : ''
    return name ? `${name} (${addr})${typeLabel}` : `${addr}${typeLabel}`
  }

  const deviceTypeFilters: FilterOption<DeviceTypeFilter>[] = [
    { value: 'all' as const, label: 'All', count: typeCounts.all },
    { value: 'blinds' as const, icon: Blinds, count: typeCounts.blinds },
    { value: 'lights' as const, icon: Lightbulb, count: typeCounts.lights },
  ]

  const columns: Column<RfPacket>[] = [
    {
      key: 'time', label: 'Time', sortable: true,
      value: (pkt) => pkt.received_at ?? pkt.t,
      render: (pkt) => <span className="text-muted-foreground">{formatTime(pkt.received_at)}</span>,
    },
    {
      key: 'source', label: 'Source', sortable: true,
      value: (pkt) => configNames[pkt.src] || pkt.src,
      render: (pkt) => <AddressCell addr={pkt.src} name={configNames[pkt.src]} deviceType={addressTypes[pkt.src] ?? 'unknown'} />,
    },
    {
      key: 'destination', label: 'Destination', sortable: true,
      value: (pkt) => configNames[pkt.dst] || pkt.dst,
      render: (pkt) => <AddressCell addr={pkt.dst} name={configNames[pkt.dst]} deviceType={addressTypes[pkt.dst] ?? 'unknown'} />,
    },
    {
      key: 'channel', label: 'CH', sortable: true,
      value: (pkt) => pkt.channel ?? 0,
      render: (pkt) => <span className="text-muted-foreground">{pkt.channel ?? '-'}</span>,
    },
    {
      key: 'type', label: 'Type', sortable: true,
      value: (pkt) => getMsgTypeLabel(pkt.type),
      render: (pkt) => <Badge variant="secondary" className="text-[10px] px-1.5 py-0">{getMsgTypeLabel(pkt.type)}</Badge>,
    },
    {
      key: 'command', label: 'Command', sortable: true,
      value: (pkt) => (isCommandPacket(pkt) || isButtonPacket(pkt)) ? getCommandLabel(pkt.command) : '',
      render: (pkt) => <span className="text-muted-foreground">{(isCommandPacket(pkt) || isButtonPacket(pkt)) ? getCommandLabel(pkt.command) : '-'}</span>,
    },
    {
      key: 'state', label: 'State', sortable: true,
      value: (pkt) => isStatusPacket(pkt) ? getStateLabel(pkt.state) : '',
      render: (pkt) => isStatusPacket(pkt) ? getStateLabel(pkt.state) : '-',
    },
    {
      key: 'rssi', label: 'RSSI', sortable: true,
      value: (pkt) => pkt.rssi ?? 0,
      render: (pkt) => <span className="text-muted-foreground">{pkt.rssi?.toFixed(1) ?? '-'}</span>,
    },
    {
      key: 'actions', label: '',
      render: (pkt) => <CopyPacketBtn pkt={pkt} />,
    },
  ]

  return (
    <div className="flex flex-col gap-4">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-2">
          <FilterBar options={deviceTypeFilters} value={deviceTypeFilter} onChange={(v) => useStore.getState().setDeviceTypeFilter(v)} />
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
                <option key={addr} value={addr}>{formatDropdownLabel(addr)}</option>
              ))}
            </select>
            <svg className="pointer-events-none absolute right-2 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m6 9 6 6 6-6"/></svg>
          </div>
        </div>
        <button
          className="inline-flex items-center gap-1.5 rounded-md border border-input bg-background px-3 py-1.5 text-xs font-medium hover:bg-accent hover:text-accent-foreground transition-colors"
          onClick={() => useStore.getState().clearRfPackets()}
        >
          <Trash2 className="size-3" />
          Clear
        </button>
      </div>

      <Card className="gap-0 overflow-hidden p-0">
        <DataTable
          columns={columns}
          data={filtered}
          rowKey={(pkt, i) => `${pkt.t}-${i}`}
          defaultSort={{ key: 'time', direction: 'desc' }}
          maxHeight="500px"
          tableClass="font-mono"
          emptyMessage="No packets"
        />
      </Card>
    </div>
  )
}
