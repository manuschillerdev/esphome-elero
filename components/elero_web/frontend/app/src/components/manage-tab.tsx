import { Fragment } from 'preact'
import { useSignal } from '@preact/signals'
import {
  createTable,
  createColumnHelper,
  getCoreRowModel,
  getSortedRowModel,
  getFilteredRowModel,
  getGroupedRowModel,
  getExpandedRowModel,
  type TableState,
  type Updater,
  type FilterFn,
  type Row,
  type Cell,
  type Header,
} from '@tanstack/table-core'
import { Button } from './ui/button'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { Badge } from './ui/badge'
import { InlineEdit } from './ui/inline-edit'
import { SignalIndicator } from './signal-indicator'
import { DiscoveryBanner } from './discovery-banner'
import { DeviceExpandedPanel } from './device-row'
import {
  Blinds, Lightbulb, LightbulbOff, RemoteControl, LayoutGrid,
  ChevronUp, ChevronDown, ChevronRight, Square, Search, X, Plus,
  Shrink, Save, Trash2,
} from './icons'
import { cn } from '@/lib/utils'
import {
  devices, groups, displayNames, getStateLabel, showToast, updateDevice, updateGroup,
  hub, rebootNeeded, command, msg_type,
  type Device, type DevicePairing, type GroupConfig,
} from '@/store'
import { sendDeviceCommand, sendGroupCommand, sendRawCommand, sendRemoveGroup, sendRestart, sendUpsertDevice, sendUpsertGroup } from '@/ws'

// ─── Row model ──────────────────────────────────────────────────────────────

const UNPAIRED_REMOTE_FILTER = '__unpaired__'

interface PairedRemote {
  address: string
  label: string
  channel: number
}

interface RemoteOption {
  address: string
  label: string
}

type StatusFilter = '' | 'saved' | 'unsaved'

type TypeFilter = '' | 'cover' | 'light'

interface Row_ {
  device: Device
  status: 'saved' | 'unsaved'
  /** Physical or virtual remotes paired to this device. Current API exposes one; UI treats it as a relationship list. */
  pairedRemotes: PairedRemote[]
  /** Searchable remote labels/addresses. */
  pairedRemoteText: string
  /** HA state name or "—" */
  haState: string
  /** rf state name (lowercased) or "—" */
  rfState: string
  /** rssi number or null */
  rssi: number | null
}

function buildPairedRemotes(device: Device, names: Record<string, string>): PairedRemote[] {
  const pairings = device.pairings.length > 0
    ? device.pairings
    : device.remote
      ? [{ remote: device.remote, channel: device.channel }]
      : []
  return pairings.map((pairing) => ({
    address: pairing.remote,
    label: names[pairing.remote] ?? pairing.remote,
    channel: pairing.channel,
  }))
}

function buildRows(devs: Map<string, Device>, names: Record<string, string>): Row_[] {
  const rows: Row_[] = []
  for (const d of devs.values()) {
    if (d.type === 'remote') continue   // remotes are relationships, not primary rows
    const pairedRemotes = buildPairedRemotes(d, names)
    rows.push({
      device: d,
      status: d.updated_at === null ? 'unsaved' : 'saved',
      pairedRemotes,
      pairedRemoteText: pairedRemotes.length > 0
        ? pairedRemotes.map((remote) => `${remote.label} ${remote.address} CH ${remote.channel}`).join(' ')
        : 'Unpaired',
      haState: ((d.lastStatus as Record<string, unknown> | null)?.ha_state as string | undefined)?.toUpperCase() ?? '—',
      rfState: getStateLabel((d.lastStatus?.state) as string | undefined),
      rssi: d.lastStatus?.rssi ?? null,
    })
  }
  return rows
}

function buildRemoteOptions(rows: Row_[]): RemoteOption[] {
  const options = new Map<string, string>()
  let hasUnpaired = false
  for (const row of rows) {
    if (row.pairedRemotes.length === 0) {
      hasUnpaired = true
      continue
    }
    for (const remote of row.pairedRemotes) options.set(remote.address, remote.label)
  }
  const remotes = Array.from(options, ([address, label]) => ({ address, label }))
    .sort((a, b) => a.label.localeCompare(b.label))
  return hasUnpaired ? [...remotes, { address: UNPAIRED_REMOTE_FILTER, label: 'Unpaired' }] : remotes
}

const pairedRemotesFilter: FilterFn<Row_> = (row, _columnId, filterValue) => {
  const selected = String(filterValue ?? '')
  if (!selected) return true
  if (selected === UNPAIRED_REMOTE_FILTER) return row.original.pairedRemotes.length === 0
  return row.original.pairedRemotes.some((remote) => remote.address === selected)
}

const statusFilter: FilterFn<Row_> = (row, _columnId, filterValue) => {
  const selected = String(filterValue ?? '')
  return !selected || row.original.status === selected
}

// ─── Type icon ──────────────────────────────────────────────────────────────

function TypeIcon({ type }: { type: Device['type'] }) {
  const Icon =
    type === 'cover' ? Blinds :
    type === 'light' ? Lightbulb :
    type === 'remote' ? RemoteControl :
    LayoutGrid
  const label =
    type === 'cover' ? 'Cover' :
    type === 'light' ? 'Light' :
    type === 'remote' ? 'Remote' :
    'Group'
  return (
    <Tooltip>
      <TooltipTrigger>
        <Icon className="size-4 text-muted-foreground" />
      </TooltipTrigger>
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  )
}

// ─── Cell renderers ────────────────────────────────────────────────────────

function StatusDot({ device }: { device: Device }) {
  const unsaved = device.updated_at === null
  const label = unsaved ? 'Unsaved' : device.enabled ? 'Saved & published' : 'Saved & unpublished'
  return (
    <Tooltip>
      <TooltipTrigger>
        <span
          className={cn(
            'inline-flex size-2 shrink-0 rounded-full',
            unsaved ? 'bg-orange-400' : device.enabled ? 'bg-emerald-500' : 'bg-muted-foreground/40',
          )}
        />
      </TooltipTrigger>
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  )
}

function NameCell({ row }: { row: Row_ }) {
  const device = row.device
  const fallback = device.type === 'cover'
    ? `Unnamed cover (${device.address})`
    : device.type === 'light'
      ? `Unnamed light (${device.address})`
      : device.address
  return (
    <div className="flex min-w-0 items-center gap-2">
      <StatusDot device={device} />
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium text-foreground">
          <InlineEdit value={device.name || fallback} onSave={(name) => updateDevice(device.address, { name })} />
        </span>
        <span className="font-mono text-[10px] text-muted-foreground">{device.address}</span>
      </div>
    </div>
  )
}

function StateCell({ row }: { row: Row_ }) {
  return (
    <div className="flex flex-col gap-0.5 text-[10px] font-medium uppercase tracking-wide text-muted-foreground">
      <span>HA {row.haState}</span>
      <span>RF {row.rfState}</span>
    </div>
  )
}

function PairedRemotesCell({ row }: { row: Row_ }) {
  const remotes = row.pairedRemotes
  if (remotes.length === 0) {
    return <span className="text-xs text-muted-foreground">Unpaired</span>
  }

  const visible = remotes.slice(0, 2)
  const hidden = remotes.length - visible.length
  return (
    <div className="flex min-w-0 flex-wrap items-center gap-1">
      {visible.map((remote) => (
        <Tooltip key={`${remote.address}:${remote.channel}`}>
          <TooltipTrigger>
            <span className="inline-flex max-w-44 items-center gap-1.5 rounded-full bg-muted px-2 py-0.5 text-[11px] font-medium text-muted-foreground">
              <span className="truncate">{remote.label}</span>
              <span className="rounded bg-background/70 px-1 font-mono text-[10px] tabular-nums">CH {remote.channel}</span>
            </span>
          </TooltipTrigger>
          <TooltipContent>{remote.address}</TooltipContent>
        </Tooltip>
      ))}
      {hidden > 0 && <Badge variant="secondary" className="h-5 px-1.5 text-[10px]">+{hidden}</Badge>}
    </div>
  )
}

// ─── Inline action buttons (compact) ────────────────────────────────────────

function SaveButton({ device }: { device: Device }) {
  if (!hub.value.crud) return null
  return (
    <Tooltip>
      <TooltipTrigger>
        <Button variant="ghost" size="icon" className="size-6 text-primary hover:text-primary" onClick={() => sendUpsertDevice(device)}>
          <Save className="size-3.5" />
        </Button>
      </TooltipTrigger>
      <TooltipContent>{device.updated_at !== null ? 'Saved — click to update' : 'Save to NVS'}</TooltipContent>
    </Tooltip>
  )
}

type ManageCommand = 'up' | 'down' | 'stop' | 'tilt'

const rawCommandByAction: Record<ManageCommand, string> = {
  up: command.UP,
  down: command.DOWN,
  stop: command.STOP,
  tilt: command.TILT,
}

function firstPairing(device: Device): DevicePairing | null {
  if (device.pairings.length > 0) return device.pairings[0]
  return device.remote ? { remote: device.remote, channel: device.channel } : null
}

function sendManageCommand(device: Device, action: ManageCommand) {
  if (device.updated_at !== null) {
    sendDeviceCommand(device, action)
    return
  }

  const pairing = firstPairing(device)
  if (!pairing) {
    showToast('error', `Cannot test ${device.name || device.address}: no paired remote/channel discovered yet`)
    return
  }

  sendRawCommand({
    dst_address: device.address,
    src_address: pairing.remote,
    channel: pairing.channel,
    command: rawCommandByAction[action],
    msg_type: device.type === 'cover' && action === 'stop' ? msg_type.COMMAND : msg_type.BUTTON,
  })
}

function InlineActions({ device }: { device: Device }) {
  if (device.type === 'cover') {
    return (
      <div className="flex items-center justify-end gap-0.5 text-primary">
        <SaveButton device={device} />
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-6 text-primary hover:text-primary disabled:text-muted-foreground/40 disabled:pointer-events-none" disabled={!device.supports_tilt} onClick={() => sendManageCommand(device, 'tilt')}>
              <Shrink className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>{device.supports_tilt ? 'Tilt' : 'Tilt (disabled)'}</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-6" onClick={() => sendManageCommand(device, 'up')}>
              <ChevronUp className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>Open</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-6" onClick={() => sendManageCommand(device, 'stop')}>
              <Square className="size-3" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>Stop</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-6" onClick={() => sendManageCommand(device, 'down')}>
              <ChevronDown className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>Close</TooltipContent>
        </Tooltip>
      </div>
    )
  }
  if (device.type === 'light') {
    return (
      <div className="flex items-center justify-end gap-0.5 text-primary">
        <SaveButton device={device} />
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-6" onClick={() => sendManageCommand(device, 'up')}>
              <Lightbulb className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>On</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-6" onClick={() => sendManageCommand(device, 'down')}>
              <LightbulbOff className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent>Off</TooltipContent>
        </Tooltip>
      </div>
    )
  }
  return null
}

// ─── Columns ────────────────────────────────────────────────────────────────

const columnHelper = createColumnHelper<Row_>()

const columns = [
  columnHelper.display({
    id: 'select',
    header: ({ table }) => (
      <input
        type="checkbox"
        className="size-3.5 cursor-pointer accent-primary"
        checked={table.getIsAllPageRowsSelected()}
        ref={(el) => { if (el) el.indeterminate = table.getIsSomePageRowsSelected() }}
        onChange={table.getToggleAllPageRowsSelectedHandler()}
      />
    ),
    cell: ({ row }) => (
      <input
        type="checkbox"
        className="size-3.5 cursor-pointer accent-primary"
        checked={row.getIsSelected()}
        disabled={!row.getCanSelect()}
        onChange={row.getToggleSelectedHandler()}
        onClick={(e) => e.stopPropagation()}
      />
    ),
  }),
  columnHelper.display({
    id: 'expand',
    cell: ({ row, table }) => {
      const meta = table.options.meta as { isDetailOpen: (id: string) => boolean; toggleDetail: (id: string) => void }
      const open = meta.isDetailOpen(row.id)
      return (
        <Button
          variant="ghost"
          size="icon"
          className="size-6"
          onClick={() => meta.toggleDetail(row.id)}
          aria-label={open ? 'Collapse' : 'Expand'}
        >
          {open ? <ChevronDown className="size-3.5" /> : <ChevronRight className="size-3.5" />}
        </Button>
      )
    },
  }),
  columnHelper.accessor((r) => r.status, {
    id: 'status',
    header: 'Status',
    filterFn: statusFilter,
  }),
  columnHelper.accessor((r) => r.device.type, {
    id: 'type',
    header: 'Type',
    cell: (info) => <TypeIcon type={info.getValue()} />,
    filterFn: 'equals',
  }),
  columnHelper.accessor((r) => r.device.name || r.device.address, {
    id: 'name',
    header: 'Name',
    cell: (info) => <NameCell row={info.row.original} />,
  }),
  columnHelper.accessor((r) => r.pairedRemoteText, {
    id: 'pairedRemotes',
    header: 'Remote / channel pairings',
    cell: (info) => <PairedRemotesCell row={info.row.original} />,
    filterFn: pairedRemotesFilter,
  }),
  columnHelper.accessor((r) => `${r.haState} ${r.rfState}`, {
    id: 'state',
    header: 'State',
    cell: (info) => <StateCell row={info.row.original} />,
  }),
  columnHelper.accessor((r) => r.rssi ?? -999, {
    id: 'rssi',
    header: 'RSSI',
    cell: (info) => {
      const r = info.row.original
      if (r.rssi == null) return <span className="text-[10px] text-muted-foreground">—</span>
      return (
        <div className="flex items-center justify-end gap-1.5 text-[10px] text-muted-foreground">
          <SignalIndicator rssi={r.rssi} />
          <span className="tabular-nums">{r.rssi.toFixed(0)} dBm</span>
        </div>
      )
    },
  }),
  columnHelper.display({
    id: 'actions',
    cell: (info) => <InlineActions device={info.row.original.device} />,
  }),
]

// ─── Toolbar ────────────────────────────────────────────────────────────────

type GroupBy = 'none' | 'type'

function Toolbar({
  search, onSearch,
  statusFilterValue, onStatusFilter,
  typeFilter, onTypeFilter,
  remoteFilter, onRemoteFilter, remoteOptions,
  groupBy, onGroupBy,
  rowCount, totalCount,
  savedCount, unsavedCount, coverCount, lightCount,
  selectedCount,
}: {
  search: string
  onSearch: (v: string) => void
  statusFilterValue: StatusFilter
  onStatusFilter: (v: StatusFilter) => void
  typeFilter: TypeFilter
  onTypeFilter: (v: TypeFilter) => void
  remoteFilter: string
  onRemoteFilter: (v: string) => void
  remoteOptions: RemoteOption[]
  groupBy: GroupBy
  onGroupBy: (v: GroupBy) => void
  rowCount: number
  totalCount: number
  savedCount: number
  unsavedCount: number
  coverCount: number
  lightCount: number
  selectedCount: number
}) {
  return (
    <div className="flex flex-wrap items-center gap-2 border-b border-border bg-muted/20 px-3 py-2">
      {/* Search */}
      <div className="relative">
        <Search className="pointer-events-none absolute left-2 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
        <input
          type="text"
          value={search}
          onInput={(e) => onSearch((e.target as HTMLInputElement).value)}
          placeholder="Search name, address, or remote…"
          className="h-7 w-56 rounded-md border border-input bg-background pl-7 pr-7 text-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
        />
        {search && (
          <button
            type="button"
            className="absolute right-1.5 top-1/2 -translate-y-1/2 text-muted-foreground hover:text-foreground"
            onClick={() => onSearch('')}
          >
            <X className="size-3" />
          </button>
        )}
      </div>

      {/* Save-state filter */}
      <select
        value={statusFilterValue}
        onChange={(e) => onStatusFilter((e.target as HTMLSelectElement).value as StatusFilter)}
        className="h-7 rounded-md border border-input bg-background px-2 text-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
      >
        <option value="">All ({totalCount})</option>
        <option value="saved">Saved ({savedCount})</option>
        <option value="unsaved">Unsaved ({unsavedCount})</option>
      </select>

      {/* Type filter */}
      <select
        value={typeFilter}
        onChange={(e) => onTypeFilter((e.target as HTMLSelectElement).value as TypeFilter)}
        className="h-7 rounded-md border border-input bg-background px-2 text-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
      >
        <option value="">All types</option>
        <option value="cover">Covers ({coverCount})</option>
        <option value="light">Lights ({lightCount})</option>
      </select>

      {/* Remote filter */}
      <select
        value={remoteFilter}
        onChange={(e) => onRemoteFilter((e.target as HTMLSelectElement).value)}
        className="h-7 rounded-md border border-input bg-background px-2 text-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
      >
        <option value="">All remotes</option>
        {remoteOptions.map((remote) => (
          <option key={remote.address} value={remote.address}>{remote.label}</option>
        ))}
      </select>

      {/* Group-by */}
      <label className="flex items-center gap-1.5 text-[11px] text-muted-foreground">
        <span>Group by</span>
        <select
          value={groupBy}
          onChange={(e) => onGroupBy((e.target as HTMLSelectElement).value as GroupBy)}
          className="h-7 rounded-md border border-input bg-background px-2 text-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
        >
          <option value="none">None</option>
          <option value="type">Type</option>
        </select>
      </label>

      <span className="text-[11px] text-muted-foreground">
        {rowCount} {rowCount === 1 ? 'device' : 'devices'}
        {selectedCount > 0 && <span className="ml-1 text-foreground">· {selectedCount} selected</span>}
      </span>

      <div className="ml-auto" />
    </div>
  )
}

// ─── Floating action bar (selection > 0) ────────────────────────────────────

function SelectionBar({
  selectedDevices, onClear, onCreateGroup,
}: {
  selectedDevices: Device[]
  onClear: () => void
  onCreateGroup: () => void
}) {
  if (selectedDevices.length === 0) return null
  return (
    <div className="fixed bottom-6 left-1/2 z-50 flex -translate-x-1/2 items-center gap-3 rounded-full border border-border bg-popover px-4 py-2 shadow-lg">
      <span className="text-xs font-medium">
        {selectedDevices.length} selected
      </span>
      <div className="h-4 w-px bg-border" />
      <Button size="sm" variant="ghost" className="h-7 gap-1 text-xs" onClick={() => sendBulk(selectedDevices, 'up')}>
        <ChevronUp className="size-3" /> Open
      </Button>
      <Button size="sm" variant="ghost" className="h-7 gap-1 text-xs" onClick={() => sendBulk(selectedDevices, 'stop')}>
        <Square className="size-3" /> Stop
      </Button>
      <Button size="sm" variant="ghost" className="h-7 gap-1 text-xs" onClick={() => sendBulk(selectedDevices, 'down')}>
        <ChevronDown className="size-3" /> Close
      </Button>
      <div className="h-4 w-px bg-border" />
      <Button
        size="sm"
        variant="default"
        className="h-7 gap-1 text-xs"
        disabled={selectedDevices.length < 2}
        onClick={onCreateGroup}
      >
        <Plus className="size-3" /> Group
      </Button>
      <Button size="sm" variant="ghost" className="size-7 p-0" onClick={onClear} aria-label="Clear selection">
        <X className="size-3.5" />
      </Button>
    </div>
  )
}

function sendBulk(devs: Device[], cmd: 'up' | 'down' | 'stop') {
  for (const d of devs) {
    if (d.type === 'cover' || d.type === 'light') {
      sendManageCommand(d, cmd)
    }
  }
}

function groupMemberDevices(group: GroupConfig, devs: Map<string, Device>): Device[] {
  return group.device_ids
    .map((id) => devs.get(id))
    .filter((device): device is Device => Boolean(device && device.type !== 'remote'))
}

function groupKind(members: Device[]): 'cover' | 'light' | 'mixed' | 'missing' {
  if (members.length === 0) return 'missing'
  const first = members[0].type
  if (first !== 'cover' && first !== 'light') return 'missing'
  return members.every((member) => member.type === first) ? first : 'mixed'
}

function GroupIcon({ kind }: { kind: ReturnType<typeof groupKind> }) {
  const Icon = kind === 'cover' ? Blinds : kind === 'light' ? Lightbulb : LayoutGrid
  return <Icon className="size-4 shrink-0 text-muted-foreground" />
}

function saveGroup(group: GroupConfig) {
  const current = groups.value.get(group.id) ?? group
  const name = current.name.trim()
  if (!name) {
    showToast('error', 'Group name is required')
    return
  }
  sendUpsertGroup({ id: current.id, name, device_ids: current.device_ids })
}

function GroupStatusDot({ group }: { group: GroupConfig }) {
  const unsaved = group.updated_at === null
  return (
    <Tooltip>
      <TooltipTrigger>
        <span className={cn('inline-flex size-2 shrink-0 rounded-full', unsaved ? 'bg-orange-400' : 'bg-emerald-500')} />
      </TooltipTrigger>
      <TooltipContent>{unsaved ? 'Unsaved' : 'Saved'}</TooltipContent>
    </Tooltip>
  )
}

function GroupsPanel({ devs }: { devs: Map<string, Device> }) {
  const savedGroups = [...groups.value.values()]
  if (savedGroups.length === 0) return null

  return (
    <div className="overflow-x-auto rounded-lg border border-border bg-card">
      <table className="w-full text-xs">
        <thead className="bg-muted text-primary">
          <tr>
            <th className="px-3 py-2 text-left font-medium">Group</th>
            <th className="px-3 py-2 text-left font-medium">Type</th>
            <th className="px-3 py-2 text-left font-medium">Members</th>
            <th className="px-3 py-2 text-right font-medium">Actions</th>
          </tr>
        </thead>
        <tbody>
          {savedGroups.map((group) => {
            const members = groupMemberDevices(group, devs)
            const kind = groupKind(members)
            const missing = group.device_ids.length - members.length
            const canCommand = kind === 'cover' || kind === 'light'
            return (
              <tr key={group.id} className="border-b border-border last:border-b-0 hover:bg-muted/20">
                <td className="px-3 py-2.5">
                  <div className="flex min-w-0 items-center gap-2">
                    <GroupStatusDot group={group} />
                    <GroupIcon kind={kind} />
                    <div className="min-w-0">
                      <div className="truncate text-sm font-medium text-foreground">
                        <InlineEdit value={group.name} onSave={(name) => updateGroup(group.id, { name })} />
                      </div>
                      <div className="font-mono text-[10px] text-muted-foreground">{group.id}</div>
                    </div>
                  </div>
                </td>
                <td className="px-3 py-2.5">
                  <Badge variant="secondary" className="h-5 px-1.5 text-[10px]">
                    {kind === 'cover' ? 'Covers' : kind === 'light' ? 'Lights' : 'Invalid'}
                  </Badge>
                </td>
                <td className="max-w-xl px-3 py-2.5">
                  <div className="flex flex-wrap gap-1">
                    {members.length > 0 ? members.map((member) => (
                      <span key={member.address} className="inline-flex max-w-40 items-center rounded-full bg-muted px-2 py-0.5 text-[11px] text-muted-foreground">
                        <span className="truncate">{member.name || member.address}</span>
                      </span>
                    )) : <span className="text-[11px] text-muted-foreground">No resolvable members</span>}
                    {missing > 0 && (
                      <span className="inline-flex items-center rounded-full bg-destructive/10 px-2 py-0.5 text-[11px] text-destructive">
                        {missing} missing
                      </span>
                    )}
                  </div>
                </td>
                <td className="px-3 py-2.5">
                  <div className="flex items-center justify-end gap-0.5 text-primary">
                    <Tooltip>
                      <TooltipTrigger>
                        <Button variant="ghost" size="icon" className="size-6 text-primary hover:text-primary" onClick={() => saveGroup(group)}>
                          <Save className="size-3.5" />
                        </Button>
                      </TooltipTrigger>
                      <TooltipContent>{group.updated_at !== null ? 'Saved — click to update' : 'Save to NVS'}</TooltipContent>
                    </Tooltip>
                    {kind === 'cover' && (
                      <>
                        <Tooltip>
                          <TooltipTrigger>
                            <Button variant="ghost" size="icon" className="size-6 text-primary hover:text-primary disabled:text-muted-foreground/40 disabled:pointer-events-none" disabled={!canCommand} onClick={() => sendGroupCommand(group.id, 'tilt')}>
                              <Shrink className="size-3.5" />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent>Tilt group</TooltipContent>
                        </Tooltip>
                        <Tooltip>
                          <TooltipTrigger>
                            <Button variant="ghost" size="icon" className="size-6" disabled={!canCommand} onClick={() => sendGroupCommand(group.id, 'up')}>
                              <ChevronUp className="size-3.5" />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent>Open group</TooltipContent>
                        </Tooltip>
                        <Tooltip>
                          <TooltipTrigger>
                            <Button variant="ghost" size="icon" className="size-6" disabled={!canCommand} onClick={() => sendGroupCommand(group.id, 'stop')}>
                              <Square className="size-3" />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent>Stop group</TooltipContent>
                        </Tooltip>
                        <Tooltip>
                          <TooltipTrigger>
                            <Button variant="ghost" size="icon" className="size-6" disabled={!canCommand} onClick={() => sendGroupCommand(group.id, 'down')}>
                              <ChevronDown className="size-3.5" />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent>Close group</TooltipContent>
                        </Tooltip>
                      </>
                    )}
                    {kind === 'light' && (
                      <>
                        <Tooltip>
                          <TooltipTrigger>
                            <Button variant="ghost" size="icon" className="size-6" disabled={!canCommand} onClick={() => sendGroupCommand(group.id, 'up')}>
                              <Lightbulb className="size-3.5" />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent>Turn group on</TooltipContent>
                        </Tooltip>
                        <Tooltip>
                          <TooltipTrigger>
                            <Button variant="ghost" size="icon" className="size-6" disabled={!canCommand} onClick={() => sendGroupCommand(group.id, 'down')}>
                              <LightbulbOff className="size-3.5" />
                            </Button>
                          </TooltipTrigger>
                          <TooltipContent>Turn group off</TooltipContent>
                        </Tooltip>
                      </>
                    )}
                    {!canCommand && <span className="mr-1 text-[11px] text-destructive">Invalid</span>}
                    <Tooltip>
                      <TooltipTrigger>
                        <Button variant="ghost" size="icon" className="size-6 text-destructive hover:text-destructive" onClick={() => sendRemoveGroup(group.id)}>
                          <Trash2 className="size-3.5" />
                        </Button>
                      </TooltipTrigger>
                      <TooltipContent>Remove group</TooltipContent>
                    </Tooltip>
                  </div>
                </td>
              </tr>
            )
          })}
        </tbody>
      </table>
    </div>
  )
}

function makeGroupId(name: string): string {
  const slug = name.trim().toLowerCase()
    .replace(/[^a-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '')
    .slice(0, 10) || 'group'
  return `grp_${slug}_${Date.now().toString(36).slice(-6)}`.slice(0, 23)
}

// ─── Create-group modal ─────────────────────────────────────────────────────

function CreateGroupModal({
  initialMembers, onClose,
}: {
  initialMembers: Device[]
  onClose: () => void
}) {
  const name = useSignal('')
  const checked = useSignal<Set<string>>(new Set(initialMembers.map((d) => d.address)))
  const names = displayNames.value

  const controllableMembers = initialMembers.filter((device) => device.type === 'cover' || device.type === 'light')
  const members = controllableMembers.filter((device) => device.updated_at !== null)
  const excludedUnsaved = controllableMembers.length - members.length
  const checkedMembers = members.filter((m) => checked.value.has(m.address))
  const checkedCount = checkedMembers.length
  const selectedTypes = new Set(checkedMembers.map((m) => m.type))
  const mixedTypes = selectedTypes.size > 1
  const valid = name.value.trim().length > 0 && checkedCount >= 2 && !mixedTypes

  const toggle = (addr: string) => {
    const next = new Set(checked.value)
    if (next.has(addr)) next.delete(addr); else next.add(addr)
    checked.value = next
  }

  const submit = () => {
    if (!valid) return
    const groupName = name.value.trim().slice(0, 23)
    const device_ids = checkedMembers.map((m) => m.address)
    sendUpsertGroup({ id: makeGroupId(groupName), name: groupName, device_ids })
    onClose()
  }

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 backdrop-blur-[2px]"
      onClick={onClose}
    >
      <div
        className="w-full max-w-md rounded-lg border border-border bg-popover p-5 shadow-xl"
        onClick={(e) => e.stopPropagation()}
        role="dialog"
        aria-label="Create group"
      >
        <div className="flex items-center justify-between gap-2">
          <h2 className="text-base font-semibold">Create group</h2>
          <Button variant="ghost" size="icon" className="size-7" onClick={onClose} aria-label="Close">
            <X className="size-4" />
          </Button>
        </div>

        <div className="mt-3 flex flex-col gap-3">
          <label className="flex flex-col gap-1 text-xs">
            <span className="text-muted-foreground">Name</span>
            <input
              type="text"
              value={name.value}
              onInput={(e) => { name.value = (e.target as HTMLInputElement).value }}
              placeholder="e.g. Wohnzimmer, NM, all-blinds…"
              className="h-8 rounded-md border border-input bg-background px-2 text-sm outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
              autoFocus
            />
          </label>

          <div className="flex flex-col gap-1 text-xs">
            <span className="text-muted-foreground">Members ({checkedCount})</span>
            <div className="max-h-[260px] overflow-y-auto rounded-md border border-border">
              <ul className="divide-y divide-border">
                {members.map((m) => {
                  const isChecked = checked.value.has(m.address)
                  return (
                    <li
                      key={m.address}
                      className={cn(
                        'flex items-center gap-2 px-2.5 py-1.5 cursor-pointer hover:bg-muted/40',
                        isChecked && 'bg-primary/5',
                      )}
                      onClick={() => toggle(m.address)}
                    >
                      <input
                        type="checkbox"
                        className="size-3.5 cursor-pointer accent-primary"
                        checked={isChecked}
                        onClick={(e) => e.stopPropagation()}
                        onChange={() => toggle(m.address)}
                      />
                      <TypeIcon type={m.type} />
                      <span className="flex-1 truncate text-sm">{m.name || m.address}</span>
                      <span className="text-[10px] text-muted-foreground">
                        {buildPairedRemotes(m, names).map((remote) => `${remote.label} · CH ${remote.channel}`).join(', ') || 'Unpaired'}
                      </span>
                    </li>
                  )
                })}
              </ul>
            </div>
          </div>

          {excludedUnsaved > 0 && (
            <div className="text-[11px] text-muted-foreground">
              {excludedUnsaved} unsaved device{excludedUnsaved === 1 ? '' : 's'} hidden. Save devices before grouping them.
            </div>
          )}

          {mixedTypes && (
            <div className="text-[11px] text-destructive">
              Groups cannot mix covers and lights.
            </div>
          )}

          {checkedCount < 2 && (
            <div className="text-[11px] text-muted-foreground">
              Select at least 2 members.
            </div>
          )}
        </div>

        <div className="mt-4 flex items-center justify-end gap-2">
          <Button variant="ghost" size="sm" onClick={onClose}>Cancel</Button>
          <Button
            variant="default"
            size="sm"
            disabled={!valid}
            onClick={submit}
            className="gap-1.5"
          >
            <Plus className="size-3.5" />
            Create
          </Button>
        </div>
      </div>
    </div>
  )
}

// ─── Page chrome ───────────────────────────────────────────────────────────

function RebootBanner() {
  if (!rebootNeeded.value || hub.value.mode !== 'native') return null
  return (
    <div className="flex items-center justify-between rounded-xl border border-orange-300 bg-orange-50 px-4 py-3 text-sm dark:border-orange-700 dark:bg-orange-950">
      <span className="text-orange-800 dark:text-orange-200">
        Reboot required for changes to take effect in Home Assistant
      </span>
      <button
        className="rounded-md bg-orange-600 px-3 py-1 text-xs font-medium text-white hover:bg-orange-700"
        onClick={() => sendRestart()}
      >
        Reboot now
      </button>
    </div>
  )
}

function EmptyState({ status, type, hasFilters }: { status: StatusFilter; type: TypeFilter; hasFilters: boolean }) {
  const title = hasFilters
    ? 'No devices match the current filters'
    : type === 'cover'
      ? 'No covers configured'
      : type === 'light'
        ? 'No lights configured'
        : status === 'unsaved'
          ? 'No unsaved changes'
          : status === 'saved'
            ? 'No saved devices'
            : 'No devices yet'
  const description = hasFilters
    ? 'Adjust the search, status, type, or remote filter.'
    : status === 'unsaved'
      ? 'Press buttons on your physical Elero remotes to discover new devices, or edit settings on existing devices. Unsaved changes will appear here.'
      : status === 'saved'
        ? 'Save discovered devices to NVS, or clear filters to inspect all devices.'
        : type === 'cover'
          ? 'Press buttons on your physical Elero remotes to discover covers, then save them to NVS.'
          : type === 'light'
            ? 'Press buttons on your physical Elero remotes to discover lights, then save them to NVS.'
            : 'Press buttons on your physical remotes to discover new addresses. If nothing appears, check your frequency and pin configuration on the Hub page.'

  return (
    <div className="p-8 text-center">
      <p className="text-sm font-medium text-foreground">{title}</p>
      <p className="mt-1 text-xs text-muted-foreground">{description}</p>
    </div>
  )
}

// ─── Main component ─────────────────────────────────────────────────────────

const INITIAL_STATE: TableState = {
  sorting: [],
  columnFilters: [],
  globalFilter: '',
  grouping: [],
  expanded: true,   // auto-expand all group rows
  rowSelection: {},
  columnVisibility: { status: false },
  columnOrder: [],
  columnPinning: { left: [], right: [] },
  rowPinning: { top: [], bottom: [] },
  columnSizing: {},
  columnSizingInfo: {
    startOffset: null, startSize: null, deltaOffset: null, deltaPercentage: null,
    isResizingColumn: false, columnSizingStart: [],
  },
  pagination: { pageIndex: 0, pageSize: 1000 },
}

export function ManageTab() {
  const allDevices = devices.value
  const names = displayNames.value
  const tableState = useSignal<TableState>(INITIAL_STATE)
  const detailOpen = useSignal<Set<string>>(new Set())

  const data = buildRows(allDevices, names)
  const remoteOptions = buildRemoteOptions(data)
  const savedCount = data.filter((row) => row.status === 'saved').length
  const unsavedCount = data.filter((row) => row.status === 'unsaved').length
  const coverCount = data.filter((row) => row.device.type === 'cover').length
  const lightCount = data.filter((row) => row.device.type === 'light').length

  const table = createTable<Row_>({
    data,
    columns,
    getRowId: (row) => row.device.address,
    state: tableState.value,
    onStateChange: (updater: Updater<TableState>) => {
      tableState.value = typeof updater === 'function'
        ? (updater as (prev: TableState) => TableState)(tableState.value)
        : updater
    },
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
    getFilteredRowModel: getFilteredRowModel(),
    getGroupedRowModel: getGroupedRowModel(),
    getExpandedRowModel: getExpandedRowModel(),
    getSubRows: undefined,
    enableRowSelection: (row) => row.original.device.type === 'cover' || row.original.device.type === 'light',
    enableMultiRowSelection: true,
    enableExpanding: true,
    autoResetPageIndex: false,
    autoResetExpanded: false,
    renderFallbackValue: null,
    globalFilterFn: 'includesString',
    groupedColumnMode: false,   // keep declared column order even when grouping
    meta: {
      isDetailOpen: (id: string) => detailOpen.value.has(id),
      toggleDetail: (id: string) => {
        const next = new Set(detailOpen.value)
        if (next.has(id)) next.delete(id); else next.add(id)
        detailOpen.value = next
      },
    },
  })

  const setGlobalFilter = (v: string) => {
    tableState.value = { ...tableState.value, globalFilter: v }
  }
  const setColumnFilter = (id: string, value: string) => {
    const others = tableState.value.columnFilters.filter((f) => f.id !== id)
    tableState.value = {
      ...tableState.value,
      columnFilters: value ? [...others, { id, value }] : others,
    }
  }
  const setStatusFilter = (v: StatusFilter) => {
    setColumnFilter('status', v)
  }
  const setTypeFilter = (v: TypeFilter) => {
    setColumnFilter('type', v)
  }
  const setRemoteFilter = (v: string) => {
    setColumnFilter('pairedRemotes', v)
  }
  const setGroupBy = (v: GroupBy) => {
    tableState.value = { ...tableState.value, grouping: v === 'none' ? [] : [v] }
  }
  const clearSelection = () => {
    tableState.value = { ...tableState.value, rowSelection: {} }
  }

  const search = tableState.value.globalFilter as string
  const statusFilterValue = (tableState.value.columnFilters.find((f) => f.id === 'status')?.value as StatusFilter) ?? ''
  const typeFilter = (tableState.value.columnFilters.find((f) => f.id === 'type')?.value as TypeFilter) ?? ''
  const remoteFilter = (tableState.value.columnFilters.find((f) => f.id === 'pairedRemotes')?.value as string) ?? ''
  const groupBy: GroupBy = tableState.value.grouping[0] === 'type' ? 'type' : 'none'

  const rowModel = table.getRowModel()
  const selectedRows = table.getSelectedRowModel().rows
  const selectedDevices = selectedRows.map((r) => r.original.device)

  const modalOpen = useSignal(false)
  const handleCreateGroup = () => { modalOpen.value = true }
  const handleModalClose = () => { modalOpen.value = false }

  return (
    <div className="flex flex-col gap-4">
      <DiscoveryBanner />
      <RebootBanner />
      <GroupsPanel devs={allDevices} />

      <div className="overflow-x-auto rounded-lg border border-border bg-card">
        <Toolbar
          search={search}
          onSearch={setGlobalFilter}
          statusFilterValue={statusFilterValue}
          onStatusFilter={setStatusFilter}
          typeFilter={typeFilter}
          onTypeFilter={setTypeFilter}
          remoteFilter={remoteFilter}
          onRemoteFilter={setRemoteFilter}
          remoteOptions={remoteOptions}
          groupBy={groupBy}
          onGroupBy={setGroupBy}
          rowCount={rowModel.rows.length}
          totalCount={data.length}
          savedCount={savedCount}
          unsavedCount={unsavedCount}
          coverCount={coverCount}
          lightCount={lightCount}
          selectedCount={selectedDevices.length}
        />
        <table className="w-full text-xs">
          <thead className="bg-muted">
            {table.getHeaderGroups().map((hg) => (
              <tr key={hg.id} className="border-b border-border">
                {hg.headers.map((h: Header<Row_, unknown>) => {
                  const sort = h.column.getIsSorted()
                  const canSort = h.column.getCanSort()
                  return (
                    <th
                      key={h.id}
                      className={cn(
                        'whitespace-nowrap px-2 py-1.5 text-left font-medium text-muted-foreground',
                        h.column.id === 'rssi' || h.column.id === 'actions' ? 'text-right' : '',
                      )}
                      style={h.column.id === 'select' || h.column.id === 'expand' ? { width: 32 } : undefined}
                    >
                      {h.isPlaceholder ? null : canSort ? (
                        <button
                          type="button"
                          className="inline-flex items-center gap-1 text-primary hover:text-primary/80"
                          onClick={h.column.getToggleSortingHandler()}
                        >
                          {renderHeaderContent(h)}
                          <SortIndicator sort={sort} />
                        </button>
                      ) : (
                        renderHeaderContent(h)
                      )}
                    </th>
                  )
                })}
              </tr>
            ))}
          </thead>
          <tbody className="divide-y divide-border">
            {rowModel.rows.map((row: Row<Row_>) => {
              if (row.getIsGrouped()) {
                return (
                  <tr key={row.id} className="bg-muted/40">
                    <td colSpan={columns.length} className="px-2 py-1.5">
                      <button
                        type="button"
                        className="inline-flex items-center gap-1.5 text-xs font-medium text-foreground"
                        onClick={() => row.toggleExpanded()}
                      >
                        {row.getIsExpanded() ? <ChevronDown className="size-3.5" /> : <ChevronRight className="size-3.5" />}
                        <span>
                          Type: <span className="text-foreground">{String(row.groupingValue)}</span>
                        </span>
                        <Badge variant="secondary" className="h-4 px-1.5 text-[10px]">{row.subRows.length}</Badge>
                      </button>
                    </td>
                  </tr>
                )
              }
              const isDetailOpen = detailOpen.value.has(row.id)
              return (
                <Fragment key={row.id}>
                  <tr
                    className={cn(
                      'transition-colors hover:bg-muted/30',
                      row.getIsSelected() && 'bg-primary/5',
                    )}
                  >
                    {row.getVisibleCells().map((cell: Cell<Row_, unknown>) => (
                      <td
                        key={cell.id}
                        className={cn(
                          'px-2 py-1.5 align-middle',
                          cell.column.id === 'rssi' || cell.column.id === 'actions' ? 'text-right' : '',
                        )}
                      >
                        {cell.getIsPlaceholder() ? null : renderCellContent(cell)}
                      </td>
                    ))}
                  </tr>
                  {isDetailOpen && (
                    <tr key={`${row.id}-exp`} className="bg-muted/10">
                      <td colSpan={columns.length} className="p-0">
                        <DeviceExpandedPanel device={row.original.device} />
                      </td>
                    </tr>
                  )}
                </Fragment>
              )
            })}
          </tbody>
        </table>

        {rowModel.rows.length === 0 && (
          <EmptyState
            status={statusFilterValue}
            type={typeFilter}
            hasFilters={Boolean(search || remoteFilter)}
          />
        )}
      </div>

      <SelectionBar
        selectedDevices={selectedDevices}
        onClear={clearSelection}
        onCreateGroup={handleCreateGroup}
      />

      {modalOpen.value && (
        <CreateGroupModal initialMembers={selectedDevices} onClose={handleModalClose} />
      )}
    </div>
  )
}

// Tanstack stores cell.column.columnDef.cell as a function or value.
// In the React adapter you'd use flexRender(); for table-core we resolve manually.

function renderCellContent(cell: Cell<Row_, unknown>) {
  const def = cell.column.columnDef.cell
  if (typeof def === 'function') {
    return (def as (ctx: ReturnType<Cell<Row_, unknown>['getContext']>) => unknown)(cell.getContext()) as never
  }
  return cell.renderValue() as never
}

function renderHeaderContent(header: Header<Row_, unknown>) {
  const def = header.column.columnDef.header
  if (typeof def === 'function') {
    return (def as (ctx: ReturnType<Header<Row_, unknown>['getContext']>) => unknown)(header.getContext()) as never
  }
  return def as never
}

// ─── Misc UI ────────────────────────────────────────────────────────────────

function SortIndicator({ sort }: { sort: 'asc' | 'desc' | false }) {
  if (!sort) {
    return (
      <span className="inline-flex flex-col opacity-30">
        <ChevronUp className="size-2.5 -mb-0.5" />
        <ChevronDown className="size-2.5" />
      </span>
    )
  }
  return sort === 'asc'
    ? <ChevronUp className="size-3" />
    : <ChevronDown className="size-3" />
}
