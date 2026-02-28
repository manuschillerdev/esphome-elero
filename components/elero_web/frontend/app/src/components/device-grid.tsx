import { BlindCard } from './blind-card'
import { BlindListRow } from './blind-list-row'
import { useStore, type DeviceStatus } from '@/store'

export function DeviceGrid() {
  const viewMode = useStore((s) => s.viewMode)
  const filter = useStore((s) => s.filter)
  const devices = useStore((s) => s.devices)

  // Filter and sort inline
  const filtered = devices.filter((d) => {
    if (filter === 'all') return true
    return d.status === filter
  })

  const sorted = [...filtered].sort((a, b) => {
    const order: Record<DeviceStatus, number> = {
      configured: 0,
      disabled: 1,
      discovered: 2,
    }
    return order[a.status] - order[b.status]
  })

  if (sorted.length === 0) {
    return (
      <div className="flex items-center justify-center rounded-xl border border-dashed border-border bg-card py-16 text-sm text-muted-foreground">
        No {filter !== 'all' ? filter : ''} devices found
      </div>
    )
  }

  if (viewMode === 'list') {
    return (
      <div className="flex flex-col gap-2">
        {sorted.map((device) => (
          <BlindListRow key={device.address} device={device} />
        ))}
      </div>
    )
  }

  return (
    <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
      {sorted.map((device) => (
        <BlindCard key={device.address} device={device} />
      ))}
    </div>
  )
}
