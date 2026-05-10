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
  type Row,
  type Cell,
  type Header,
} from '@tanstack/table-core'
import { Button } from './ui/button'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { Badge } from './ui/badge'
import { SignalIndicator } from './signal-indicator'
import { DeviceExpandedPanel } from './device-row'
import {
  Blinds, Lightbulb, RemoteControl, Users,
  ChevronUp, ChevronDown, ChevronRight, Square, Search, X, Plus,
} from './icons'
import { cn } from '@/lib/utils'
import {
  devices, displayNames, getStateLabel,
  type Device,
} from '@/store'
import { sendDeviceCommand, sendCreateGroup } from '@/ws'

// ─── Row model ──────────────────────────────────────────────────────────────

interface Row_ {
  device: Device
  /** display name of the owning remote, or "—" */
  remoteName: string
  /** rf state name (lowercased) or "—" */
  rfState: string
  /** rssi number or null */
  rssi: number | null
}

function buildRows(devs: Map<string, Device>, names: Record<string, string>): Row_[] {
  const rows: Row_[] = []
  for (const d of devs.values()) {
    if (d.type === 'remote') continue   // remotes shown only as group headers / chips
    rows.push({
      device: d,
      remoteName: d.remote ? (names[d.remote] ?? d.remote) : '—',
      rfState: getStateLabel((d.lastStatus?.state) as string | undefined),
      rssi: d.lastStatus?.rssi ?? null,
    })
  }
  return rows
}

// ─── Type icon ──────────────────────────────────────────────────────────────

function TypeIcon({ type }: { type: Device['type'] }) {
  const Icon =
    type === 'cover' ? Blinds :
    type === 'light' ? Lightbulb :
    type === 'remote' ? RemoteControl :
    Users
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

// ─── State pill ─────────────────────────────────────────────────────────────

function StatePill({ row }: { row: Row_ }) {
  const dev = row.device
  const unsaved = dev.updated_at === null
  const state = row.rfState
  return (
    <div className="flex items-center gap-1.5">
      <span
        className={cn(
          'inline-flex size-1.5 shrink-0 rounded-full',
          unsaved ? 'bg-orange-400' : dev.enabled ? 'bg-emerald-500' : 'bg-muted-foreground/40',
        )}
      />
      <span className="text-[10px] font-medium uppercase tracking-wide text-muted-foreground">{state}</span>
    </div>
  )
}

// ─── Inline action buttons (compact) ────────────────────────────────────────

function InlineActions({ device }: { device: Device }) {
  if (device.type === 'cover') {
    return (
      <div className="flex items-center justify-end gap-0.5 text-primary">
        <Button variant="ghost" size="icon" className="size-6" onClick={() => sendDeviceCommand(device, 'up')}>
          <ChevronUp className="size-3.5" />
        </Button>
        <Button variant="ghost" size="icon" className="size-6" onClick={() => sendDeviceCommand(device, 'stop')}>
          <Square className="size-3" />
        </Button>
        <Button variant="ghost" size="icon" className="size-6" onClick={() => sendDeviceCommand(device, 'down')}>
          <ChevronDown className="size-3.5" />
        </Button>
      </div>
    )
  }
  if (device.type === 'light') {
    return (
      <div className="flex items-center justify-end gap-0.5 text-primary">
        <Button variant="ghost" size="icon" className="size-6" onClick={() => sendDeviceCommand(device, 'up')}>
          <ChevronUp className="size-3.5" />
        </Button>
        <Button variant="ghost" size="icon" className="size-6" onClick={() => sendDeviceCommand(device, 'down')}>
          <ChevronDown className="size-3.5" />
        </Button>
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
  columnHelper.accessor((r) => r.device.type, {
    id: 'type',
    header: 'Type',
    cell: (info) => <TypeIcon type={info.getValue()} />,
    filterFn: 'equals',
  }),
  columnHelper.accessor((r) => r.device.name || r.device.address, {
    id: 'name',
    header: 'Name',
    cell: (info) => (
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium text-foreground">{info.getValue()}</span>
        <span className="font-mono text-[10px] text-muted-foreground">{info.row.original.device.address}</span>
      </div>
    ),
  }),
  columnHelper.accessor((r) => r.remoteName, {
    id: 'remote',
    header: 'Remote',
    cell: (info) => (
      <span className="truncate text-xs text-muted-foreground">{info.getValue()}</span>
    ),
  }),
  columnHelper.accessor((r) => r.device.channel, {
    id: 'channel',
    header: 'CH',
    cell: (info) => (
      <span className="font-mono text-xs tabular-nums text-muted-foreground">{info.getValue()}</span>
    ),
  }),
  columnHelper.accessor((r) => r.rfState, {
    id: 'state',
    header: 'State',
    cell: (info) => <StatePill row={info.row.original} />,
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

type GroupBy = 'none' | 'remote' | 'type'

function Toolbar({
  search, onSearch,
  typeFilter, onTypeFilter,
  groupBy, onGroupBy,
  rowCount,
  onCreateGroup, selectedCount,
}: {
  search: string
  onSearch: (v: string) => void
  typeFilter: string
  onTypeFilter: (v: string) => void
  groupBy: GroupBy
  onGroupBy: (v: GroupBy) => void
  rowCount: number
  selectedCount: number
  onCreateGroup: () => void
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
          placeholder="Search name or address…"
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

      {/* Type filter */}
      <select
        value={typeFilter}
        onChange={(e) => onTypeFilter((e.target as HTMLSelectElement).value)}
        className="h-7 rounded-md border border-input bg-background px-2 text-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
      >
        <option value="">All types</option>
        <option value="cover">Covers</option>
        <option value="light">Lights</option>
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
          <option value="remote">Remote</option>
          <option value="type">Type</option>
        </select>
      </label>

      <span className="text-[11px] text-muted-foreground">
        {rowCount} {rowCount === 1 ? 'device' : 'devices'}
        {selectedCount > 0 && <span className="ml-1 text-foreground">· {selectedCount} selected</span>}
      </span>

      <div className="ml-auto flex items-center gap-2">
        <Button
          size="sm"
          variant="outline"
          className="h-7 gap-1.5 text-xs"
          disabled={selectedCount < 2}
          onClick={onCreateGroup}
        >
          <Plus className="size-3" />
          Create group
        </Button>
      </div>
    </div>
  )
}

// ─── Floating action bar (selection > 0) ────────────────────────────────────

function SelectionBar({
  selectedDevices, remotes, onClear, onCreateGroup,
}: {
  selectedDevices: Device[]
  remotes: number
  onClear: () => void
  onCreateGroup: () => void
}) {
  if (selectedDevices.length === 0) return null
  return (
    <div className="fixed bottom-6 left-1/2 z-50 flex -translate-x-1/2 items-center gap-3 rounded-full border border-border bg-popover px-4 py-2 shadow-lg">
      <span className="text-xs font-medium">
        {selectedDevices.length} selected
        {remotes > 1 && <span className="ml-1 text-muted-foreground">· {remotes} remotes</span>}
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
      sendDeviceCommand(d, cmd)
    }
  }
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

  const members = initialMembers
  const checkedCount = checked.value.size
  const remoteCount = new Set(
    members.filter((m) => checked.value.has(m.address)).map((m) => m.remote),
  ).size
  const valid = name.value.trim().length > 0 && checkedCount >= 2

  const toggle = (addr: string) => {
    const next = new Set(checked.value)
    if (next.has(addr)) next.delete(addr); else next.add(addr)
    checked.value = next
  }

  const submit = () => {
    if (!valid) return
    const addrs = members.filter((m) => checked.value.has(m.address)).map((m) => m.address)
    sendCreateGroup(name.value.trim(), addrs)
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
                      <span className="text-[10px] text-muted-foreground">CH {m.channel}</span>
                      <span className="text-[10px] text-muted-foreground">{m.remote}</span>
                    </li>
                  )
                })}
              </ul>
            </div>
          </div>

          {remoteCount > 1 && checkedCount >= 2 && (
            <div className="rounded-md border border-orange-300/50 bg-orange-50/50 px-3 py-2 text-[11px] text-orange-900 dark:border-orange-700/30 dark:bg-orange-950/20 dark:text-orange-200">
              Cross-remote group: hub will emit <strong>{remoteCount}</strong> RF packets per command, one per remote.
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

        <p className="mt-3 text-[10px] text-muted-foreground">
          Backend persistence lands in PR-B; for now this submits a stub WS message.
        </p>
      </div>
    </div>
  )
}

// ─── Main component ─────────────────────────────────────────────────────────

const INITIAL_STATE: TableState = {
  sorting: [],
  columnFilters: [],
  globalFilter: '',
  grouping: ['remote'],
  expanded: true,   // auto-expand all group rows
  rowSelection: {},
  columnVisibility: {},
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

  const table = createTable<Row_>({
    data,
    columns,
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
  const setTypeFilter = (v: string) => {
    const others = tableState.value.columnFilters.filter((f) => f.id !== 'type')
    tableState.value = {
      ...tableState.value,
      columnFilters: v ? [...others, { id: 'type', value: v }] : others,
    }
  }
  const setGroupBy = (v: GroupBy) => {
    tableState.value = { ...tableState.value, grouping: v === 'none' ? [] : [v] }
  }
  const clearSelection = () => {
    tableState.value = { ...tableState.value, rowSelection: {} }
  }

  const search = tableState.value.globalFilter as string
  const typeFilter = (tableState.value.columnFilters.find((f) => f.id === 'type')?.value as string) ?? ''
  const groupBy: GroupBy = tableState.value.grouping[0] === 'remote' ? 'remote'
    : tableState.value.grouping[0] === 'type' ? 'type' : 'none'

  const rowModel = table.getRowModel()
  const selectedRows = table.getSelectedRowModel().rows
  const selectedDevices = selectedRows.map((r) => r.original.device)
  const selectedRemotes = new Set(selectedDevices.map((d) => d.remote)).size

  const modalOpen = useSignal(false)
  const handleCreateGroup = () => { modalOpen.value = true }
  const handleModalClose = () => { modalOpen.value = false }

  return (
    <div className="flex flex-col gap-0">
      <Toolbar
        search={search}
        onSearch={setGlobalFilter}
        typeFilter={typeFilter}
        onTypeFilter={setTypeFilter}
        groupBy={groupBy}
        onGroupBy={setGroupBy}
        rowCount={data.length}
        selectedCount={selectedDevices.length}
        onCreateGroup={handleCreateGroup}
      />

      <div className="overflow-x-auto rounded-lg border border-border bg-card">
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
                          {groupBy === 'remote' ? 'Remote: ' : 'Type: '}
                          <span className="text-foreground">{String(row.groupingValue)}</span>
                        </span>
                        <Badge variant="secondary" className="h-4 px-1.5 text-[10px]">{row.subRows.length}</Badge>
                      </button>
                    </td>
                  </tr>
                )
              }
              const isDetailOpen = detailOpen.value.has(row.id)
              return (
                <>
                  <tr
                    key={row.id}
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
                </>
              )
            })}
          </tbody>
        </table>

        {rowModel.rows.length === 0 && (
          <div className="p-8 text-center text-sm text-muted-foreground">
            No devices match the current filters.
          </div>
        )}
      </div>

      <SelectionBar
        selectedDevices={selectedDevices}
        remotes={selectedRemotes}
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
