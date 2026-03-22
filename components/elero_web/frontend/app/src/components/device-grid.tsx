import { RemoteGroup } from './remote-group'
import { deviceGroups, filters as filtersSignal } from '@/store'

function EmptyState({ title, description }: { title: string; description: string }) {
  return (
    <div className="rounded-xl border border-dashed border-border bg-muted/20 px-6 py-10 text-center">
      <p className="text-sm font-medium text-foreground">{title}</p>
      <p className="mt-1 text-xs text-muted-foreground">{description}</p>
    </div>
  )
}

export function DeviceGrid() {
  const groups = deviceGroups.value
  const { status: statusFilter, deviceType: deviceTypeFilter } = filtersSignal.value

  return (
    <div className="flex flex-col gap-4">
      {groups.map((group) => (
        <RemoteGroup key={group.remote.address} group={group} />
      ))}

      {groups.length === 0 && statusFilter === 'unsaved' && (
        <EmptyState
          title="No unsaved changes"
          description="Press buttons on your physical Elero remotes to discover new devices, or edit settings on existing devices. Unsaved changes will appear here."
        />
      )}

      {groups.length === 0 && statusFilter === 'saved' && (
        <EmptyState
          title={
            deviceTypeFilter === 'covers' ? 'No saved covers'
            : deviceTypeFilter === 'lights' ? 'No saved lights'
            : 'No saved devices'
          }
          description="Save discovered devices to NVS using the save button, or add devices to your ESPHome YAML configuration."
        />
      )}

      {groups.length === 0 && statusFilter === 'all' && (
        <EmptyState
          title={
            deviceTypeFilter === 'covers' ? 'No covers configured'
            : deviceTypeFilter === 'lights' ? 'No lights configured'
            : 'No devices yet'
          }
          description={
            deviceTypeFilter === 'covers'
              ? 'Add a cover with platform: elero to your ESPHome YAML, specifying dst_address, src_address, and channel. Then reflash.'
              : deviceTypeFilter === 'lights'
              ? 'Add a light with platform: elero to your ESPHome YAML, specifying dst_address, src_address, and channel. Then reflash.'
              : 'Add devices to your ESPHome YAML configuration, or press buttons on your physical remotes to discover new addresses. If nothing appears, check your frequency and pin configuration on the Hub page.'
          }
        />
      )}
    </div>
  )
}
