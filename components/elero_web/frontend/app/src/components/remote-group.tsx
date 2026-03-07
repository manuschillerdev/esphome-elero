import { InlineEdit } from './ui/inline-edit'
import { Badge } from './ui/badge'
import { DataTable, type Column } from './ui/data-table'
import { RemoteControl } from './icons'
import { DeviceCell, LightDeviceCell, StateCell, SignalCell, BlindActions, LightActions } from './device-row'
import { useStore, getStateLabel, type BlindConfig, type LightConfig } from '@/store'

type DeviceRow =
  | { type: 'blind'; address: string; name: string; channel: number; data: BlindConfig }
  | { type: 'light'; address: string; name: string; channel: number; data: LightConfig }

interface RemoteGroupProps {
  address: string
  blinds: BlindConfig[]
  lights: LightConfig[]
}

export function RemoteGroup({ address, blinds, lights }: RemoteGroupProps) {
  const remoteName = useStore((s) => s.remoteNames[address])
  const setRemoteName = useStore((s) => s.setRemoteName)
  const states = useStore((s) => s.states)
  const deviceCount = blinds.length + lights.length

  const rows: DeviceRow[] = [
    ...blinds.map((b) => ({ type: 'blind' as const, address: b.address, name: b.name, channel: b.channel, data: b })),
    ...lights.map((l) => ({ type: 'light' as const, address: l.address, name: l.name, channel: l.channel, data: l })),
  ]

  const columns: Column<DeviceRow>[] = [
    {
      key: 'device',
      label: 'Device',
      sortable: true,
      value: (row) => row.name,
      headerClass: 'px-4',
      cellClass: 'px-4 py-2.5',
      render: (row) => row.type === 'blind'
        ? <DeviceCell blind={row.data as BlindConfig} />
        : <LightDeviceCell light={row.data as LightConfig} />,
    },
    {
      key: 'state',
      label: 'State',
      sortable: true,
      value: (row) => getStateLabel(states[row.address]?.state),
      cellClass: 'py-2.5',
      render: (row) => <StateCell address={row.address} />,
    },
    {
      key: 'signal',
      label: 'Signal',
      sortable: true,
      value: (row) => states[row.address]?.rssi ?? -999,
      cellClass: 'py-2.5',
      render: (row) => <SignalCell address={row.address} />,
    },
    {
      key: 'actions',
      label: 'Actions',
      align: 'right',
      cellClass: 'py-2.5',
      render: (row) => row.type === 'blind'
        ? <BlindActions blind={row.data as BlindConfig} />
        : <LightActions light={row.data as LightConfig} />,
    },
  ]

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
          <Badge variant="default" className="text-[10px] px-1.5 py-0 tabular-nums">
            {deviceCount}
          </Badge>
        </div>
        <div className="flex items-center gap-2">
          {remoteName && (
            <Badge variant="secondary" className="font-mono text-[9px] tracking-wider text-muted-foreground px-1.5 py-0">
              {address}
            </Badge>
          )}
          <div className="flex size-6 items-center justify-center rounded-md bg-muted text-muted-foreground">
            <RemoteControl className="size-3.5" />
          </div>
        </div>
      </div>

      {/* Device table */}
      <DataTable
        columns={columns}
        data={rows}
        rowKey={(row) => row.address}
        defaultSort={{ key: 'device', direction: 'asc' }}
      />
    </div>
  )
}
