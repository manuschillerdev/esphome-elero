import { signal } from '@preact/signals'
import { Lightbulb, Blinds, Download } from './icons'
import { FilterBar, type FilterOption } from './ui/filter-bar'
import { Button } from './ui/button'
import { filters as filtersSignal, filterCounts, setStatusFilter, setDeviceTypeFilter, exportYaml, type StatusFilter, type DeviceTypeFilter } from '@/store'

const copyFeedback = signal<string | null>(null)

function handleExport() {
  const yaml = exportYaml()
  if (!yaml.trim()) return
  navigator.clipboard.writeText(yaml).then(() => {
    copyFeedback.value = 'Copied!'
    setTimeout(() => { copyFeedback.value = null }, 2000)
  })
}

export function ControlBar() {
  const { status: statusFilter, deviceType: deviceTypeFilter } = filtersSignal.value
  const counts = filterCounts.value
  const feedback = copyFeedback.value

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
    <div className="flex items-center justify-between gap-2">
      <div className="flex items-center gap-2">
        <FilterBar options={filters} value={statusFilter} onChange={setStatusFilter} />
        <FilterBar options={deviceTypes} value={deviceTypeFilter} onChange={setDeviceTypeFilter} />
      </div>
      {counts.saved > 0 && (
        <Button
          variant="outline"
          size="sm"
          onClick={handleExport}
          title="Copy ESPHome YAML for all saved devices to clipboard"
        >
          <Download className="size-3.5" />
          {feedback ?? 'Export YAML'}
        </Button>
      )}
    </div>
  )
}
