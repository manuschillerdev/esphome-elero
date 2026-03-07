import { Lightbulb, Blinds } from './icons'
import { FilterBar, type FilterOption } from './ui/filter-bar'
import { useStore, buildFilterCounts, type FilterState, type DeviceTypeFilter } from '@/store'

export function ControlBar() {
  const filter = useStore((s) => s.filter)
  const deviceTypeFilter = useStore((s) => s.deviceTypeFilter)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)
  const states = useStore((s) => s.states)
  const counts = buildFilterCounts(blinds, lights, states)

  const filters: FilterOption<FilterState>[] = [
    { value: 'all', label: 'All', count: counts.all },
    { value: 'configured', label: 'Configured', count: counts.configured },
    { value: 'discovered', label: 'Discovered', count: counts.discovered },
  ]

  const deviceTypes: FilterOption<DeviceTypeFilter>[] = [
    { value: 'all', label: 'All' },
    { value: 'blinds', icon: Blinds, count: counts.blinds },
    { value: 'lights', icon: Lightbulb, count: counts.lights },
  ]

  return (
    <div className="flex items-center gap-2">
      <FilterBar options={filters} value={filter} onChange={(v) => useStore.getState().setFilter(v)} />
      <FilterBar options={deviceTypes} value={deviceTypeFilter} onChange={(v) => useStore.getState().setDeviceTypeFilter(v)} />
    </div>
  )
}
