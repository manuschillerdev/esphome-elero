import { Lightbulb, Blinds } from './icons'
import { FilterBar, type FilterOption } from './ui/filter-bar'
import { ui, filterCounts, setStatusFilter, setDeviceTypeFilter, type StatusFilter, type DeviceTypeFilter } from '@/store'

export function ControlBar() {
  const { status: statusFilter, deviceType: deviceTypeFilter } = ui.value.filters
  const counts = filterCounts.value

  const filters: FilterOption<StatusFilter>[] = [
    { value: 'all', label: 'All', count: counts.all },
    { value: 'saved', label: 'Saved', count: counts.saved },
    { value: 'unsaved', label: 'Unsaved', count: counts.unsaved },
  ]

  const deviceTypes: FilterOption<DeviceTypeFilter>[] = [
    { value: 'all', label: 'All' },
    { value: 'covers', icon: Blinds, count: counts.covers },
    { value: 'lights', icon: Lightbulb, count: counts.lights },
  ]

  return (
    <div className="flex items-center gap-2">
      <FilterBar options={filters} value={statusFilter} onChange={setStatusFilter} />
      <FilterBar options={deviceTypes} value={deviceTypeFilter} onChange={setDeviceTypeFilter} />
    </div>
  )
}
