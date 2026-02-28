import { useEffect } from 'preact/compat'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { cn } from '@/lib/utils'
import { Radio, Clock, LayoutGrid, List } from './icons'
import { useStore, type FilterState } from '@/store'

function formatTimer(seconds: number): string {
  const m = Math.floor(seconds / 60)
  const s = seconds % 60
  return `${m}:${s.toString().padStart(2, '0')}`
}

export function ControlBar() {
  const filter = useStore((s) => s.filter)
  const setFilter = useStore((s) => s.setFilter)
  const viewMode = useStore((s) => s.viewMode)
  const setViewMode = useStore((s) => s.setViewMode)
  const discoveryActive = useStore((s) => s.discoveryActive)
  const toggleDiscovery = useStore((s) => s.toggleDiscovery)
  const discoveryElapsed = useStore((s) => s.discoveryElapsed)
  const devices = useStore((s) => s.devices)

  // Compute counts inline
  const counts = {
    all: devices.length,
    configured: devices.filter((d) => d.status === 'configured').length,
    disabled: devices.filter((d) => d.status === 'disabled').length,
    discovered: devices.filter((d) => d.status === 'discovered').length,
  }

  useEffect(() => {
    if (!discoveryActive) {
      useStore.getState().resetDiscoveryElapsed()
      return
    }
    const interval = setInterval(() => {
      useStore.getState().incrementDiscoveryElapsed()
    }, 1000)
    return () => clearInterval(interval)
  }, [discoveryActive])

  const filters: { value: FilterState; label: string }[] = [
    { value: 'all', label: 'All' },
    { value: 'configured', label: 'Enabled' },
    { value: 'disabled', label: 'Disabled' },
    { value: 'discovered', label: 'Discovered' },
  ]

  return (
    <div className="flex items-center justify-between">
      {/* Filter pills */}
      <div className="flex items-center gap-1 rounded-lg bg-muted p-1">
        {filters.map((f) => (
          <button
            key={f.value}
            onClick={() => setFilter(f.value)}
            className={cn(
              'flex items-center gap-1.5 rounded-md px-3 py-1.5 text-xs font-medium transition-colors',
              filter === f.value
                ? 'bg-card text-card-foreground shadow-sm'
                : 'text-muted-foreground hover:text-foreground'
            )}
          >
            {f.label}
            <span
              className={cn(
                'tabular-nums text-[10px]',
                filter === f.value ? 'text-muted-foreground' : 'text-muted-foreground/60'
              )}
            >
              {counts[f.value]}
            </span>
          </button>
        ))}
      </div>

      <div className="flex items-center gap-2">
        {/* View toggle */}
        <div className="flex items-center rounded-lg bg-muted p-1">
          <button
            onClick={() => setViewMode('grid')}
            className={cn(
              'flex items-center justify-center rounded-md p-1.5 transition-colors',
              viewMode === 'grid'
                ? 'bg-card text-card-foreground shadow-sm'
                : 'text-muted-foreground hover:text-foreground'
            )}
            aria-label="Grid view"
          >
            <LayoutGrid className="size-3.5" />
          </button>
          <button
            onClick={() => setViewMode('list')}
            className={cn(
              'flex items-center justify-center rounded-md p-1.5 transition-colors',
              viewMode === 'list'
                ? 'bg-card text-card-foreground shadow-sm'
                : 'text-muted-foreground hover:text-foreground'
            )}
            aria-label="List view"
          >
            <List className="size-3.5" />
          </button>
        </div>

        {/* Discovery toggle */}
        <Button
          variant={discoveryActive ? 'default' : 'outline'}
          size="sm"
          onClick={toggleDiscovery}
          className={cn(
            'gap-2 text-xs font-medium',
            discoveryActive && 'bg-primary text-primary-foreground'
          )}
        >
          <Radio className={cn('size-3.5', discoveryActive && 'animate-pulse')} />
          Discovery
          {discoveryActive && (
            <Badge
              variant="secondary"
              className="ml-0.5 gap-1 bg-primary-foreground/15 px-1.5 py-0 font-mono text-[10px] text-primary-foreground"
            >
              <Clock className="size-2.5" />
              {formatTimer(discoveryElapsed)}
            </Badge>
          )}
        </Button>
      </div>
    </div>
  )
}
