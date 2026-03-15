import { useSignal } from '@preact/signals'
import { Button } from './ui/button'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { InlineEdit } from './ui/inline-edit'
import { Badge } from './ui/badge'
import { DataTable, type Column } from './ui/data-table'
import { Save, CheckCircle2, Trash2 } from './icons'
import { StatusDot, DeviceCell, LightDeviceCell, StateCell, SignalCell, BlindControls, LightControls, DeviceActions, DeviceExpandedPanel } from './device-row'
import {
  hub, updateDevice, getStateLabel,
  type Device, type DeviceGroup,
} from '@/store'
import { sendUpsertDevice, sendRemoveDevice } from '@/ws'

export function RemoteGroup({ group }: { group: DeviceGroup }) {
  const { remote, devices } = group
  const crudEnabled = hub.value.crud
  const expandedAddr = useSignal<string | null>(null)

  // ─── Device table ───────────────────────────────────────────────────────

  const deviceColumns: Column<Device>[] = [
    {
      key: 'device', label: 'Device', sortable: true,
      value: (row) => row.name,
      headerClass: 'px-4 w-full', cellClass: 'px-4 py-2.5',
      render: (row) => row.type === 'cover'
        ? <DeviceCell device={row} />
        : <LightDeviceCell device={row} />,
    },
    {
      key: 'state', label: 'State', sortable: true,
      value: (row) => getStateLabel(row.lastStatus?.state),
      headerClass: 'whitespace-nowrap', cellClass: 'py-2.5 whitespace-nowrap',
      render: (row) => <StateCell device={row} />,
    },
    {
      key: 'signal', label: 'Signal', sortable: true,
      value: (row) => row.lastStatus?.rssi ?? -999,
      headerClass: 'whitespace-nowrap', cellClass: 'py-2.5 whitespace-nowrap',
      render: (row) => <SignalCell device={row} />,
    },
    {
      key: 'controls', label: 'Controls',
      headerClass: 'whitespace-nowrap', cellClass: 'py-2.5 whitespace-nowrap',
      render: (row) => row.type === 'cover'
        ? <BlindControls device={row} />
        : <LightControls device={row} />,
    },
    {
      key: 'actions', label: 'Actions',
      headerClass: 'whitespace-nowrap', cellClass: 'py-2.5 whitespace-nowrap',
      render: (row) => {
        const isExpanded = expandedAddr.value === row.address
        const toggle = () => { expandedAddr.value = isExpanded ? null : row.address }
        return <DeviceActions device={row} expanded={isExpanded} onToggleExpand={toggle} />
      },
    },
  ]

  // ─── Render ──────────────────────────────────────────────────────────

  return (
    <div className="overflow-hidden rounded-xl border border-border bg-card shadow-sm">
      <div className="flex items-center justify-between px-4 py-2.5 bg-muted/40">
        <div className="flex items-center gap-2">
          <StatusDot device={remote} />
          <span className="text-sm font-semibold text-card-foreground">
            <InlineEdit
              value={remote.name || `Unnamed remote (${remote.address})`}
              onSave={(name) => updateDevice(remote.address, { name })}
            />
          </span>
          <Badge variant="secondary" className="text-[10px] px-1.5 py-0 tabular-nums">
            {devices.length}
          </Badge>
        </div>
        <div className="flex items-center gap-2">
          {remote.name && (
            <Badge variant="secondary" className="font-mono text-[9px] tracking-wider text-muted-foreground px-1.5 py-0">
              {remote.address}
            </Badge>
          )}
          {crudEnabled && (
            <div className="flex items-center gap-1">
              <Tooltip>
                <TooltipTrigger>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-7 text-primary hover:text-primary"
                    onClick={() => sendUpsertDevice(remote)}
                  >
                    {remote.updated_at !== null
                      ? <CheckCircle2 className="size-3.5" />
                      : <Save className="size-3.5" />}
                  </Button>
                </TooltipTrigger>
                <TooltipContent align="end">{remote.updated_at !== null ? 'Saved — click to update' : 'Save remote to NVS'}</TooltipContent>
              </Tooltip>
              {remote.updated_at !== null && (
                <Tooltip>
                  <TooltipTrigger>
                    <Button
                      variant="ghost"
                      size="icon"
                      className="size-7 text-primary hover:text-primary"
                      onClick={() => sendRemoveDevice(remote.address, 'remote')}
                    >
                      <Trash2 className="size-3.5" />
                    </Button>
                  </TooltipTrigger>
                  <TooltipContent align="end">Remove remote from NVS</TooltipContent>
                </Tooltip>
              )}
            </div>
          )}
        </div>
      </div>

      <DataTable
        columns={deviceColumns}
        data={devices}
        rowKey={(row) => row.address}
        expandedRow={(row) => expandedAddr.value === row.address ? <DeviceExpandedPanel device={row} /> : null}
      />
    </div>
  )
}
