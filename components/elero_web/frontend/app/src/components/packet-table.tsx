import { useSignal } from '@preact/signals'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { DataTable, type Column } from './ui/data-table'
import { Blinds, Lightbulb, Copy, CheckCircle2, RemoteControl } from './icons'
import {
  isStatusPacket, isCommandPacket, isButtonPacket,
  getMsgTypeLabel, getCommandLabel, getStateLabel,
  type RfPacket, type DeviceType,
} from '@/store'
import { cn } from '@/lib/utils'

// ─── Helpers ────────────────────────────────────────────────────────────────

export function formatTime(epochMs: number | undefined): string {
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

export function CopyPacketBtn({ pkt }: { pkt: RfPacket }) {
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
            copied.value ? 'text-success' : 'text-primary/60 hover:text-primary hover:bg-muted'
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

export function AddressCell({ addr, name, deviceType }: { addr: string; name?: string; deviceType: DeviceType }) {
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

// ─── Full column set (used by Debug tab) ────────────────────────────────────

export function buildFullColumns(
  configNames: Record<string, string>,
  addressTypes: Record<string, DeviceType>,
): Column<RfPacket>[] {
  return [
    {
      key: 'time', label: 'Time', sortable: true,
      value: (pkt) => pkt.received_at ?? pkt.t,
      render: (pkt) => <span className="text-muted-foreground">{formatTime(pkt.received_at)}</span>,
    },
    {
      key: 'source', label: 'Source', sortable: true, filter: 'select',
      value: (pkt) => configNames[pkt.src] || pkt.src,
      render: (pkt) => <AddressCell addr={pkt.src} name={configNames[pkt.src]} deviceType={addressTypes[pkt.src] ?? 'unknown'} />,
    },
    {
      key: 'destination', label: 'Destination', sortable: true, filter: 'select',
      value: (pkt) => configNames[pkt.dst] || pkt.dst,
      render: (pkt) => <AddressCell addr={pkt.dst} name={configNames[pkt.dst]} deviceType={addressTypes[pkt.dst] ?? 'unknown'} />,
    },
    {
      key: 'channel', label: 'CH', sortable: true, filter: 'select',
      value: (pkt) => pkt.channel != null ? String(pkt.channel) : '',
      render: (pkt) => <span className="text-muted-foreground">{pkt.channel ?? '-'}</span>,
    },
    {
      key: 'type', label: 'Type', sortable: true, filter: 'select',
      value: (pkt) => getMsgTypeLabel(pkt.type),
      render: (pkt) => <span className="text-muted-foreground">{getMsgTypeLabel(pkt.type)}</span>,
    },
    {
      key: 'command', label: 'Command', sortable: true, filter: 'select',
      value: (pkt) => (isCommandPacket(pkt) || isButtonPacket(pkt)) ? getCommandLabel(pkt.command) : '',
      render: (pkt) => <span className="text-muted-foreground">{(isCommandPacket(pkt) || isButtonPacket(pkt)) ? getCommandLabel(pkt.command) : '-'}</span>,
    },
    {
      key: 'state', label: 'State', sortable: true, filter: 'select',
      value: (pkt) => isStatusPacket(pkt) ? getStateLabel(pkt.state) : '',
      render: (pkt) => <span className="text-muted-foreground">{isStatusPacket(pkt) ? getStateLabel(pkt.state) : '-'}</span>,
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
}

// ─── Compact columns (used by per-remote collapsible section) ───────────────

export function buildCompactColumns(
  configNames: Record<string, string>,
  addressTypes: Record<string, DeviceType>,
): Column<RfPacket>[] {
  return [
    {
      key: 'time', label: 'Time', sortable: true,
      value: (pkt) => pkt.received_at ?? pkt.t,
      render: (pkt) => <span className="text-muted-foreground">{formatTime(pkt.received_at)}</span>,
    },
    {
      key: 'source', label: 'From', sortable: true,
      value: (pkt) => configNames[pkt.src] || pkt.src,
      render: (pkt) => <AddressCell addr={pkt.src} name={configNames[pkt.src]} deviceType={addressTypes[pkt.src] ?? 'unknown'} />,
    },
    {
      key: 'type', label: 'Type', sortable: true,
      value: (pkt) => getMsgTypeLabel(pkt.type),
      render: (pkt) => <span className="text-muted-foreground">{getMsgTypeLabel(pkt.type)}</span>,
    },
    {
      key: 'detail', label: 'Detail', sortable: true,
      value: (pkt) => {
        if (isStatusPacket(pkt)) return getStateLabel(pkt.state)
        if (isCommandPacket(pkt) || isButtonPacket(pkt)) return getCommandLabel(pkt.command)
        return ''
      },
      render: (pkt) => {
        if (isStatusPacket(pkt)) return <span className="text-muted-foreground">{getStateLabel(pkt.state)}</span>
        if (isCommandPacket(pkt) || isButtonPacket(pkt)) return <span className="text-muted-foreground">{getCommandLabel(pkt.command)}</span>
        return <span className="text-muted-foreground">-</span>
      },
    },
    {
      key: 'rssi', label: 'RSSI', sortable: true,
      value: (pkt) => pkt.rssi ?? 0,
      render: (pkt) => <span className="text-muted-foreground">{pkt.rssi?.toFixed(1) ?? '-'}</span>,
    },
  ]
}
