import { useSignal } from '@preact/signals'
import { InlineEdit } from './ui/inline-edit'
import { Badge } from './ui/badge'
import { FilterBar, type FilterOption } from './ui/filter-bar'
import { DataTable, type Column } from './ui/data-table'
import { RemoteControl, Radio } from './icons'
import { DeviceCell, LightDeviceCell, StateCell, SignalCell, BlindActions, LightActions } from './device-row'
import { formatTime, CopyPacketBtn, AddressCell } from './packet-table'
import {
  useStore, buildConfigNames, buildAddressTypes, getStateLabel,
  isStatusPacket, isCommandPacket, isButtonPacket,
  getMsgTypeLabel, getCommandLabel,
  type BlindConfig, type LightConfig, type RfPacket,
} from '@/store'

type DeviceRow =
  | { type: 'blind'; address: string; name: string; channel: number; data: BlindConfig }
  | { type: 'light'; address: string; name: string; channel: number; data: LightConfig }

type ViewMode = 'devices' | 'packets'

interface RemoteGroupProps {
  address: string
  blinds: BlindConfig[]
  lights: LightConfig[]
}

const viewOptions: FilterOption<ViewMode>[] = [
  { value: 'devices', icon: RemoteControl },
  { value: 'packets', icon: Radio },
]

export function RemoteGroup({ address, blinds, lights }: RemoteGroupProps) {
  const remoteName = useStore((s) => s.remoteNames[address])
  const setRemoteName = useStore((s) => s.setRemoteName)
  const states = useStore((s) => s.states)
  const rfPackets = useStore((s) => s.rfPackets)
  const allBlinds = useStore((s) => s.config.blinds)
  const allLights = useStore((s) => s.config.lights)
  const remoteNames = useStore((s) => s.remoteNames)
  const view = useSignal<ViewMode>('devices')
  const deviceCount = blinds.length + lights.length

  // All addresses belonging to this remote group
  const groupAddresses = new Set([
    address,
    ...blinds.map((b) => b.address),
    ...lights.map((l) => l.address),
  ])

  // ─── Device table ───────────────────────────────────────────────────────

  const deviceRows: DeviceRow[] = [
    ...blinds.map((b) => ({ type: 'blind' as const, address: b.address, name: b.name, channel: b.channel, data: b })),
    ...lights.map((l) => ({ type: 'light' as const, address: l.address, name: l.name, channel: l.channel, data: l })),
  ]

  const deviceColumns: Column<DeviceRow>[] = [
    {
      key: 'device', label: 'Device', sortable: true,
      value: (row) => row.name,
      headerClass: 'px-4', cellClass: 'px-4 py-2.5',
      render: (row) => row.type === 'blind'
        ? <DeviceCell blind={row.data as BlindConfig} />
        : <LightDeviceCell light={row.data as LightConfig} />,
    },
    {
      key: 'state', label: 'State', sortable: true,
      value: (row) => getStateLabel(states[row.address]?.state),
      cellClass: 'py-2.5',
      render: (row) => <StateCell address={row.address} />,
    },
    {
      key: 'signal', label: 'Signal', sortable: true,
      value: (row) => states[row.address]?.rssi ?? -999,
      cellClass: 'py-2.5',
      render: (row) => <SignalCell address={row.address} />,
    },
    {
      key: 'actions', label: 'Actions', align: 'right',
      cellClass: 'py-2.5',
      render: (row) => row.type === 'blind'
        ? <BlindActions blind={row.data as BlindConfig} />
        : <LightActions light={row.data as LightConfig} />,
    },
  ]

  // ─── Packet table ──────────────────────────────────────────────────────

  const groupPackets = rfPackets.filter(
    (p) => groupAddresses.has(p.src) || groupAddresses.has(p.dst)
  ).slice(-50).reverse()

  const configNames = buildConfigNames(allBlinds, allLights, remoteNames)
  const addressTypes = buildAddressTypes(allBlinds, allLights, rfPackets)

  const packetColumns: Column<RfPacket>[] = [
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
        return configNames[pkt.src] || pkt.src
      },
      render: (pkt) => {
        if ((isCommandPacket(pkt) || isButtonPacket(pkt)) && pkt.echo) {
          return <span className="flex items-center gap-1"><Radio className="size-2.5 text-muted-foreground" /><span>Hub</span></span>
        }
        return <AddressCell addr={pkt.src} name={configNames[pkt.src]} deviceType={addressTypes[pkt.src] ?? 'unknown'} />
      },
    },
    {
      key: 'destination', label: 'To', sortable: true,
      value: (pkt) => configNames[pkt.dst] || pkt.dst,
      render: (pkt) => <AddressCell addr={pkt.dst} name={configNames[pkt.dst]} deviceType={addressTypes[pkt.dst] ?? 'unknown'} />,
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

  // ─── Render ────────────────────────────────────────────────────────────

  return (
    <div className="overflow-hidden rounded-xl border border-border bg-card shadow-sm">
      {/* Remote header */}
      <div className="flex items-center justify-between bg-muted/40 px-4 py-2.5">
        <div className="flex items-center gap-2">
          <span className="text-sm font-semibold text-card-foreground">
            <InlineEdit
              value={remoteName || `Unnamed remote (${address})`}
              onSave={(name) => setRemoteName(address, name)}
            />
          </span>
          <Badge variant="secondary" className="text-[10px] px-1.5 py-0 tabular-nums">
            {deviceCount}
          </Badge>
        </div>
        <div className="flex items-center gap-2">
          {remoteName && (
            <Badge variant="secondary" className="font-mono text-[9px] tracking-wider text-muted-foreground px-1.5 py-0">
              {address}
            </Badge>
          )}
          <FilterBar
            options={viewOptions}
            value={view.value}
            onChange={(v) => { view.value = v }}
          />
        </div>
      </div>

      {/* Toggled view */}
      {view.value === 'devices' ? (
        <DataTable
          columns={deviceColumns}
          data={deviceRows}
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
