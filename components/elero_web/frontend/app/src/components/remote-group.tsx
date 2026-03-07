import { useSignal } from '@preact/signals'
import { InlineEdit } from './ui/inline-edit'
import { Badge } from './ui/badge'
import { FilterBar, type FilterOption } from './ui/filter-bar'
import { DataTable, type Column } from './ui/data-table'
import { RemoteControl, Radio } from './icons'
import { DeviceCell, LightDeviceCell, StateCell, SignalCell, BlindActions, LightActions } from './device-row'
import { formatTime, CopyPacketBtn, AddressCell } from './packet-table'
import { cn } from '@/lib/utils'
import {
  rfPackets, hub, displayNames, deviceTypeMap, updateDevice,
  getStateLabel, isStatusPacket, isCommandPacket, isButtonPacket,
  getMsgTypeLabel, getCommandLabel,
  type Device, type DeviceGroup, type RfPacketWithTimestamp,
} from '@/store'

export type GroupVariant = 'persisted' | 'discovered' | 'disabled'

type ViewMode = 'devices' | 'packets'

const viewOptions: FilterOption<ViewMode>[] = [
  { value: 'devices', icon: RemoteControl },
  { value: 'packets', icon: Radio },
]

const variantStyles: Record<GroupVariant, { border: string; header: string; indicator?: string; badge?: string }> = {
  persisted: {
    border: 'border-border',
    header: 'bg-muted/40',
  },
  discovered: {
    border: 'border-success/40',
    header: 'bg-success/5',
    indicator: 'bg-success',
    badge: 'text-success border-success/40',
  },
  disabled: {
    border: 'border-border opacity-50',
    header: 'bg-muted/20',
  },
}

export function RemoteGroup({ group }: { group: DeviceGroup }) {
  const { remoteAddress, remoteName, devices, variant } = group
  const allPackets = rfPackets.value
  const crudEnabled = hub.value.crud
  const names = displayNames.value
  const types = deviceTypeMap.value
  const view = useSignal<ViewMode>('devices')
  const deviceCount = devices.length
  const styles = variantStyles[variant]

  const groupAddresses = new Set([
    remoteAddress,
    ...devices.map((d) => d.address),
  ])

  // ─── Device table ───────────────────────────────────────────────────────

  const deviceColumns: Column<Device>[] = [
    {
      key: 'device', label: 'Device', sortable: true,
      value: (row) => row.name,
      headerClass: 'px-4', cellClass: 'px-4 py-2.5',
      render: (row) => row.type === 'cover'
        ? <DeviceCell device={row} />
        : <LightDeviceCell device={row} />,
    },
    {
      key: 'state', label: 'State', sortable: true,
      value: (row) => getStateLabel(row.lastStatus?.state),
      cellClass: 'py-2.5',
      render: (row) => <StateCell device={row} />,
    },
    {
      key: 'signal', label: 'Signal', sortable: true,
      value: (row) => row.lastStatus?.rssi ?? -999,
      cellClass: 'py-2.5',
      render: (row) => <SignalCell device={row} />,
    },
    {
      key: 'actions', label: 'Actions', align: 'right',
      cellClass: 'py-2.5',
      render: (row) => {
        const canSave = variant === 'discovered' && crudEnabled
        return row.type === 'cover'
          ? <BlindActions device={row} showSave={canSave} />
          : <LightActions device={row} showSave={canSave} />
      },
    },
  ]

  // ─── Packet table ──────────────────────────────────────────────────────

  const groupPackets = allPackets.filter(
    (p) => groupAddresses.has(p.src) || groupAddresses.has(p.dst)
  ).slice(-50).reverse()

  const packetColumns: Column<RfPacketWithTimestamp>[] = [
    {
      key: 'time', label: 'Time', sortable: true,
      value: (pkt) => pkt.received_at ?? pkt.t,
      render: (pkt) => <span className="text-muted-foreground">{formatTime(pkt.received_at)}</span>,
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
      key: 'source', label: 'From', sortable: true,
      value: (pkt) => {
        if ((isCommandPacket(pkt) || isButtonPacket(pkt)) && pkt.echo) return 'Hub'
        return names[pkt.src] || pkt.src
      },
      render: (pkt) => {
        if ((isCommandPacket(pkt) || isButtonPacket(pkt)) && pkt.echo) {
          return <span className="flex items-center gap-1"><Radio className="size-2.5 text-muted-foreground" /><span>Hub</span></span>
        }
        return <AddressCell addr={pkt.src} name={names[pkt.src]} deviceType={types[pkt.src] ?? 'unknown'} />
      },
    },
    {
      key: 'destination', label: 'To', sortable: true,
      value: (pkt) => names[pkt.dst] || pkt.dst,
      render: (pkt) => <AddressCell addr={pkt.dst} name={names[pkt.dst]} deviceType={types[pkt.dst] ?? 'unknown'} />,
    },
    {
      key: 'channel', label: 'CH', sortable: true,
      value: (pkt) => pkt.channel ?? 0,
      render: (pkt) => <span className="text-muted-foreground">{pkt.channel ?? '-'}</span>,
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

  // ─── Render ──────────────────────────────────────────────────────────

  return (
    <div className={cn('overflow-hidden rounded-xl border bg-card shadow-sm', styles.border)}>
      <div className={cn('flex items-center justify-between px-4 py-2.5', styles.header)}>
        <div className="flex items-center gap-2">
          {styles.indicator && (
            <span className="relative flex size-2">
              <span className={cn('absolute inline-flex size-full animate-ping rounded-full opacity-75', styles.indicator)} />
              <span className={cn('relative inline-flex size-2 rounded-full', styles.indicator)} />
            </span>
          )}
          <span className="text-sm font-semibold text-card-foreground">
            <InlineEdit
              value={remoteName !== remoteAddress ? remoteName : `Unnamed remote (${remoteAddress})`}
              onSave={(name) => updateDevice(remoteAddress, { name })}
            />
          </span>
          <Badge variant="secondary" className="text-[10px] px-1.5 py-0 tabular-nums">
            {deviceCount}
          </Badge>
        </div>
        <div className="flex items-center gap-2">
          {styles.badge && (
            <Badge variant="outline" className={cn('text-[10px] px-1.5 py-0', styles.badge)}>
              NEW
            </Badge>
          )}
          {remoteName !== remoteAddress && (
            <Badge variant="secondary" className="font-mono text-[9px] tracking-wider text-muted-foreground px-1.5 py-0">
              {remoteAddress}
            </Badge>
          )}
          <FilterBar
            options={viewOptions}
            value={view.value}
            onChange={(v) => { view.value = v }}
          />
        </div>
      </div>

      {view.value === 'devices' ? (
        <DataTable
          columns={deviceColumns}
          data={devices}
          rowKey={(row) => row.address}
          defaultSort={{ key: 'device', direction: 'asc' }}
        />
      ) : (
        <DataTable
          columns={packetColumns}
          data={groupPackets}
          rowKey={(pkt, i) => `${pkt.t}-${i}`}
          defaultSort={{ key: 'time', direction: 'desc' }}
          maxHeight="400px"
          tableClass="font-mono"
          emptyMessage="No packets yet — press buttons on this remote or send a command to one of its devices to see traffic here."
        />
      )}
    </div>
  )
}
