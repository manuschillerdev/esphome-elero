import { useSignal, useComputed } from '@preact/signals'
import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'
import { ChevronUp, ChevronDown } from '../icons'

// ─── Types ──────────────────────────────────────────────────────────────────

export interface Column<T> {
  key: string
  label: string
  sortable?: boolean
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
}

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
}: DataTableProps<T>) {
  const sort = useSignal<SortState | null>(defaultSort ?? null)

  const handleSort = (col: Column<T>) => {
    if (!col.sortable) return
    const prev = sort.value
    if (prev?.key === col.key) {
      sort.value = prev.direction === 'asc' ? { key: col.key, direction: 'desc' } : null
    } else {
      sort.value = { key: col.key, direction: 'asc' }
    }
  }

  const s = sort.value
  const sorted = (() => {
    if (!s) return data
    const col = columns.find((c) => c.key === s.key)
    if (!col?.value) return data

    const valueFn = col.value
    return [...data].sort((a, b) => {
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

  return (
    <div className={cn(maxHeight && 'overflow-y-auto', className)} style={maxHeight ? { maxHeight } : undefined}>
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
