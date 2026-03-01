import { useMemo } from 'preact/hooks'
import { Button } from './ui/button'
import { cn } from '@/lib/utils'
import { Radio, LayoutGrid, List, Lightbulb, Blinds } from './icons'
import { useStore, type FilterState, type DeviceTypeFilter } from '@/store'

export function ControlBar() {
  const filter = useStore((s) => s.filter)
  const viewMode = useStore((s) => s.viewMode)
  const deviceTypeFilter = useStore((s) => s.deviceTypeFilter)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)
  const states = useStore((s) => s.states)

  // Compute discovered addresses (in states but not in config)
  const counts = useMemo(() => {
    const configuredAddrs = new Set([
      ...blinds.map((b) => b.address),
      ...lights.map((l) => l.address),
    ])
    const discoveredAddrs = Object.keys(states).filter((addr) => !configuredAddrs.has(addr))
    return {
      all: blinds.length + lights.length + discoveredAddrs.length,
      configured: blinds.length + lights.length,
      discovered: discoveredAddrs.length,
      blinds: blinds.length,
      lights: lights.length,
    }
  }, [blinds, lights, states])

  const filters: { value: FilterState; label: string }[] = [
    { value: 'all', label: 'All' },
    { value: 'configured', label: 'Configured' },
    { value: 'discovered', label: 'Discovered' },
  ]

  const deviceTypes: { value: DeviceTypeFilter; label: string; icon?: typeof Blinds }[] = [
    { value: 'all', label: 'All' },
    { value: 'blinds', label: 'Blinds', icon: Blinds },
    { value: 'lights', label: 'Lights', icon: Lightbulb },
  ]

  return (
    <div className="flex items-center justify-between">
      <div className="flex items-center gap-2">
        {/* Filter pills */}
        <div className="flex items-center gap-1 rounded-lg bg-muted p-1">
          {filters.map((f) => (
            <button
              key={f.value}
              onClick={() => useStore.getState().setFilter(f.value)}
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

        {/* Device type filter */}
        <div className="flex items-center gap-1 rounded-lg bg-muted p-1">
          {deviceTypes.map((dt) => {
            const Icon = dt.icon
            return (
              <button
                key={dt.value}
                onClick={() => useStore.getState().setDeviceTypeFilter(dt.value)}
                className={cn(
                  'flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-xs font-medium transition-colors',
                  deviceTypeFilter === dt.value
                    ? 'bg-card text-card-foreground shadow-sm'
                    : 'text-muted-foreground hover:text-foreground'
                )}
              >
                {Icon && <Icon className="size-3.5" />}
                {!Icon && dt.label}
                {dt.value !== 'all' && (
                  <span
                    className={cn(
                      'tabular-nums text-[10px]',
                      deviceTypeFilter === dt.value ? 'text-muted-foreground' : 'text-muted-foreground/60'
                    )}
                  >
                    {counts[dt.value]}
                  </span>
                )}
              </button>
            )
          })}
        </div>
      </div>

      <div className="flex items-center gap-2">
        {/* View toggle */}
        <div className="flex items-center rounded-lg bg-muted p-1">
          <button
            onClick={() => useStore.getState().setViewMode('grid')}
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
            onClick={() => useStore.getState().setViewMode('list')}
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

        {/* Discovery button */}
        <Button
          variant="outline"
          size="sm"
          className="gap-2 text-xs font-medium"
        >
          <Radio className="size-3.5" />
          Discovery
        </Button>
      </div>
    </div>
  )
}
