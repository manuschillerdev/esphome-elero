import { useSignal } from '@preact/signals'
import { Button } from './ui/button'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { InlineEdit } from './ui/inline-edit'
import { Badge } from './ui/badge'
import { DataTable, type Column } from './ui/data-table'
import { Save, ChevronUp, Square, ChevronDown, Shrink } from './icons'
import { StatusDot, DeviceCell, LightDeviceCell, HaStateCell, RfStateCell, SignalCell, BlindControls, LightControls, DeviceActions, DeviceExpandedPanel } from './device-row'
import {
  hub, updateDevice, getStateLabel,
  type Device, type DeviceGroup,
} from '@/store'
import { sendUpsertDevice, sendRawCommand } from '@/ws'

export function RemoteGroup({ group }: { group: DeviceGroup }) {
  const { remote, devices } = group
  const crudEnabled = hub.value.crud
  const expandedAddr = useSignal<string | null>(null)
  const broadcastChannel = useSignal<number | null>(null)

  const broadcast = (command: string) => {
    const ch = broadcastChannel.value
    if (ch == null || ch < 1) return
    sendRawCommand({
      dst_address: '0x000000',
      src_address: remote.address,
      channel: ch,
      command,
      msg_type: '0x44',
      type2: '0x10',
      hop: '0x00',
    })
  }

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
      key: 'ha_state', label: 'HA State', sortable: true,
      value: (row) => (row.lastStatus as Record<string, unknown> | null)?.ha_state as string ?? '',
      headerClass: 'whitespace-nowrap', cellClass: 'py-2.5 whitespace-nowrap',
      render: (row) => <HaStateCell device={row} />,
    },
    {
      key: 'state', label: 'RF Blind State', sortable: true,
      value: (row) => getStateLabel(row.lastStatus?.state),
      headerClass: 'whitespace-nowrap', cellClass: 'py-2.5 whitespace-nowrap',
      render: (row) => <RfStateCell device={row} />,
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
          {/* Broadcast controls: channel input + action buttons */}
          <div className="flex items-center gap-1">
            <label className="flex items-center gap-1 text-xs text-muted-foreground">
              CH
              <input
                type="number"
                min={1} max={99}
                placeholder="—"
                value={broadcastChannel.value ?? ''}
                onInput={(e) => {
                  const v = parseInt((e.target as HTMLInputElement).value)
                  broadcastChannel.value = isNaN(v) ? null : v
                }}
                className="w-10 rounded border border-input bg-background px-1.5 py-0.5 text-xs tabular-nums text-center"
              />
            </label>
            <div className="flex items-center gap-0.5 ml-1">
              {([['0x24', Shrink, 'Tilt'], ['0x20', ChevronUp, 'Up'], ['0x10', Square, 'Stop'], ['0x40', ChevronDown, 'Down']] as const).map(([cmd, Icon, label]) => (
                <Tooltip key={cmd}><TooltipTrigger>
                  <Button variant="ghost" size="icon" className="size-6" disabled={!broadcastChannel.value || broadcastChannel.value < 1} onClick={() => broadcast(cmd)}>
                    <Icon className={cmd === '0x10' ? 'size-2.5' : 'size-3'} />
                  </Button>
                </TooltipTrigger><TooltipContent>{label}{broadcastChannel.value ? ` (CH ${broadcastChannel.value})` : ''}</TooltipContent></Tooltip>
              ))}
            </div>
          </div>

          {remote.name && (
            <Badge variant="secondary" className="font-mono text-[9px] tracking-wider text-muted-foreground px-1.5 py-0">
              {remote.address}
            </Badge>
          )}
          {crudEnabled && (
            <div className="flex items-center gap-1">
              <label className="flex items-center gap-1.5 text-xs cursor-pointer mr-1">
                <button
                  type="button"
                  role="switch"
                  aria-checked={remote.enabled}
                  onClick={() => updateDevice(remote.address, { enabled: !remote.enabled })}
                  className={`relative inline-flex h-4 w-7 shrink-0 rounded-full border-2 border-transparent transition-colors ${remote.enabled ? 'bg-primary' : 'bg-input'}`}
                >
                  <span className={`pointer-events-none block size-3 rounded-full bg-background shadow-sm transition-transform ${remote.enabled ? 'translate-x-3' : 'translate-x-0'}`} />
                </button>
                <span className="text-muted-foreground">Active</span>
              </label>
              <Tooltip>
                <TooltipTrigger>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-7 text-primary hover:text-primary"
                    onClick={() => sendUpsertDevice(remote)}
                  >
                    <Save className="size-3.5" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent align="end">{remote.updated_at !== null ? 'Saved — click to update' : 'Save remote to NVS'}</TooltipContent>
              </Tooltip>
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
