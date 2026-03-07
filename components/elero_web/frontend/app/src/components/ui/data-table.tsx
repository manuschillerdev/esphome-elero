import { useSignal } from '@preact/signals'
import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'
import { ChevronUp, ChevronDown, Filter } from '../icons'

// ─── Types ──────────────────────────────────────────────────────────────────

export interface Column<T> {
  key: string
  label: string
  sortable?: boolean
  /** Filter mode: 'select' = dropdown exact match, 'text' = substring search (requires `value`) */
  filter?: 'select' | 'text'
  /** Extract a sortable/filterable value from the row */
  value?: (row: T) => string | number
  /** Custom render for the cell */
  render: (row: T) => ComponentChildren
  /** Header alignment */
  align?: 'left' | 'right'
  /** Header className */
  headerClass?: string
  /** Cell className */
  cellClass?: string
}

export type SortDirection = 'asc' | 'desc' | null

interface SortState {
  key: string
  direction: 'asc' | 'desc'
}

interface DataTableProps<T> {
  columns: Column<T>[]
  data: T[]
  rowKey: (row: T, index: number) => string
  /** Extra className for the wrapping div */
  className?: string
  /** Extra className for the table element */
  tableClass?: string
  /** Max height with scroll (e.g. "500px") */
  maxHeight?: string
  /** Show empty state message */
  emptyMessage?: string
  /** Default sort state */
  defaultSort?: SortState
  /** Optional extra row content (actions column, etc.) */
  rowClass?: (row: T) => string
  /** Set to false to hide the filter row entirely (default: true if any column has `filter` set) */
  filterable?: boolean
  /** Extra content rendered in the toolbar (right side, before filter toggle) */
  toolbar?: ComponentChildren
}

// ─── Filter select style ────────────────────────────────────────────────────

const filterSelectClass =
  'h-6 w-full appearance-none rounded-md border border-input bg-background bg-[length:16px_16px] bg-[right_2px_center] bg-no-repeat pl-1.5 pr-5 text-[11px] outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px] font-sans'

const filterSelectBg = "url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%236b7280' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E\")"

// ─── Component ──────────────────────────────────────────────────────────────

export function DataTable<T>({
  columns,
  data,
  rowKey,
  className,
  tableClass,
  maxHeight,
  defaultSort,
  emptyMessage = 'No data',
  rowClass,
  filterable,
  toolbar,
}: DataTableProps<T>) {
  const hasFilterableCols = columns.some((c) => c.filter && c.value)
  const showFilters = filterable !== undefined ? filterable : hasFilterableCols
  const sort = useSignal<SortState | null>(defaultSort ?? null)
  const filters = useSignal<Record<string, string>>({})

  const handleSort = (col: Column<T>) => {
    if (!col.sortable) return
    const prev = sort.value
    if (prev?.key === col.key) {
      sort.value = prev.direction === 'asc' ? { key: col.key, direction: 'desc' } : null
    } else {
      sort.value = { key: col.key, direction: 'asc' }
    }
  }

  // Build filter options from data (unique values per filterable column)
  const filterOptions: Record<string, string[]> = {}
  const filterableCols = columns.filter((c) => c.filter && c.value)
  if (showFilters && filterableCols.length > 0) {
    for (const col of filterableCols) {
      if (col.filter !== 'select') continue
      const seen = new Set<string>()
      for (const row of data) {
        const v = String(col.value!(row))
        if (v) seen.add(v)
      }
      filterOptions[col.key] = Array.from(seen).sort()
    }
  }

  // Apply filters then sort
  const f = filters.value
  const activeFilters = Object.entries(f).filter(([, v]) => v)

  const filtered = activeFilters.length === 0
    ? data
    : data.filter((row) =>
        activeFilters.every(([key, val]) => {
          const col = columns.find((c) => c.key === key)
          if (!col?.value) return true
          const cellVal = String(col.value(row))
          if (col.filter === 'text') return cellVal.toLowerCase().includes(val.toLowerCase())
          return cellVal === val
        })
      )

  const s = sort.value
  const sorted = (() => {
    if (!s) return filtered
    const col = columns.find((c) => c.key === s.key)
    if (!col?.value) return filtered

    const valueFn = col.value
    return [...filtered].sort((a, b) => {
      const va = valueFn(a)
      const vb = valueFn(b)
      let cmp = 0
      if (typeof va === 'number' && typeof vb === 'number') {
        cmp = va - vb
      } else {
        cmp = String(va).localeCompare(String(vb))
      }
      return s.direction === 'desc' ? -cmp : cmp
    })
  })()

  const activeFilterCount = Object.values(f).filter(Boolean).length

  return (
    <div className={cn(className)}>
      {(showFilters || toolbar) && (
        <div className="flex items-center justify-end gap-1.5 border-b border-border px-3 py-1.5 bg-muted/30">
          {toolbar}
          {showFilters && activeFilterCount > 0 && (
            <button
              className="inline-flex items-center gap-1.5 rounded-md border border-transparent px-2 py-1 text-[11px] font-medium text-muted-foreground hover:text-foreground hover:bg-accent transition-colors"
              onClick={() => { filters.value = {} }}
            >
              <Filter className="size-3" />
              Reset filters
              <span className="flex size-4 items-center justify-center rounded-full bg-primary text-[9px] font-bold text-primary-foreground">
                {activeFilterCount}
              </span>
            </button>
          )}
        </div>
      )}
      <div className={cn(maxHeight && 'overflow-y-auto')} style={maxHeight ? { maxHeight } : undefined}>
      <table className={cn('w-full text-xs', tableClass)}>
        <thead className="bg-muted sticky top-0 z-10">
          <tr>
            {columns.map((col) => (
              <th
                key={col.key}
                className={cn(
                  'px-3 py-2 font-medium text-muted-foreground',
                  col.align === 'right' ? 'text-right' : 'text-left',
                  col.headerClass,
                )}
              >
                {col.sortable ? (
                  <button
                    type="button"
                    className="inline-flex items-center gap-1 text-primary cursor-pointer select-none hover:text-primary/80 transition-colors outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-1 rounded"
                    onClick={() => handleSort(col)}
                  >
                    {col.label}
                    <SortIndicator active={s?.key === col.key} direction={s?.key === col.key ? s.direction : null} />
                  </button>
                ) : (
                  <span>{col.label}</span>
                )}
              </th>
            ))}
          </tr>
          {showFilters && (
            <tr className="border-t border-border bg-muted/60">
              {columns.map((col) => (
                <th key={col.key} className="px-2 py-1.5">
                  {col.filter === 'select' && filterOptions[col.key] ? (
                    <select
                      value={f[col.key] || ''}
                      onChange={(e) => {
                        const val = (e.target as HTMLSelectElement).value
                        filters.value = { ...f, [col.key]: val }
                      }}
                      className={cn(filterSelectClass, !f[col.key] && 'text-muted-foreground')}
                      style={{ backgroundImage: filterSelectBg }}
                    >
                      <option value="">All</option>
                      {filterOptions[col.key].map((v) => (
                        <option key={v} value={v}>{v}</option>
                      ))}
                    </select>
                  ) : col.filter === 'text' ? (
                    <input
                      type="text"
                      value={f[col.key] || ''}
                      onInput={(e) => {
                        const val = (e.target as HTMLInputElement).value
                        filters.value = { ...f, [col.key]: val }
                      }}
                      placeholder="Filter…"
                      className={cn(filterSelectClass, !f[col.key] && 'text-muted-foreground')}
                    />
                  ) : null}
                </th>
              ))}
            </tr>
          )}
        </thead>
        <tbody className="divide-y divide-border">
          {sorted.map((row, i) => (
            <tr
              key={rowKey(row, i)}
              className={cn('transition-colors hover:bg-muted/30', rowClass?.(row))}
            >
              {columns.map((col) => (
                <td
                  key={col.key}
                  className={cn(
                    'px-3 py-2',
                    col.align === 'right' && 'text-right',
                    col.cellClass,
                  )}
                >
                  {col.render(row)}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      {sorted.length === 0 && (
        <div className="p-8 text-center text-sm text-muted-foreground">{emptyMessage}</div>
      )}
      </div>
    </div>
  )
}

// ─── Sort Indicator ─────────────────────────────────────────────────────────

function SortIndicator({ active, direction }: { active?: boolean; direction: SortDirection }) {
  if (!active || !direction) {
    return (
      <span className="inline-flex flex-col opacity-30">
        <ChevronUp className="size-2.5 -mb-0.5" />
        <ChevronDown className="size-2.5" />
      </span>
    )
  }
  return (
    <span className="inline-flex flex-col text-foreground">
      {direction === 'asc'
        ? <ChevronUp className="size-3" />
        : <ChevronDown className="size-3" />}
    </span>
  )
}
