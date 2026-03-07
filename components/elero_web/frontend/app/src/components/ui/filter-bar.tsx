import type { ComponentChildren } from 'preact'
import { cn } from '@/lib/utils'

export interface FilterOption<T extends string> {
  value: T
  label?: string
  icon?: (props: { className?: string }) => ComponentChildren
  count?: number
}

interface FilterBarProps<T extends string> {
  options: FilterOption<T>[]
  value: T
  onChange: (value: T) => void
}

export function FilterBar<T extends string>({ options, value, onChange }: FilterBarProps<T>) {
  return (
    <div className="flex items-center gap-1 rounded-lg bg-muted p-1">
      {options.map((opt) => {
        const active = value === opt.value
        return (
          <button
            key={opt.value}
            onClick={() => onChange(opt.value)}
            className={cn(
              'flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-xs font-medium transition-colors',
              active
                ? 'bg-card text-primary shadow-sm'
                : 'text-muted-foreground hover:text-primary'
            )}
          >
            {opt.icon ? <opt.icon className="size-3.5" /> : opt.label}
            {opt.count !== undefined && (
              <span
                className={cn(
                  'tabular-nums text-[10px]',
                  active ? 'text-muted-foreground' : 'text-muted-foreground/60'
                )}
              >
                {opt.count}
              </span>
            )}
          </button>
        )
      })}
    </div>
  )
}
