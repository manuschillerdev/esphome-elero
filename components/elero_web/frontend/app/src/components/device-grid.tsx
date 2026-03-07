import { RemoteGroup } from './remote-group'
import { deviceGroups, ui } from '@/store'

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
  const { status: statusFilter, deviceType: deviceTypeFilter } = ui.value.filters

  return (
    <div className="flex flex-col gap-4">
      {groups.map((group) => (
        <RemoteGroup key={group.remoteAddress} group={group} />
      ))}

      {groups.length === 0 && statusFilter === 'discovered' && (
        <EmptyState
          title="No devices discovered yet"
          description="Press buttons on your physical Elero remotes — new RF addresses will appear here automatically. If nothing shows up, check your frequency and pin configuration on the Hub page."
        />
      )}

      {groups.length === 0 && statusFilter === 'configured' && (
        <EmptyState
          title={
            deviceTypeFilter === 'covers' ? 'No covers configured'
            : deviceTypeFilter === 'lights' ? 'No lights configured'
            : 'No devices configured'
          }
          description={
            deviceTypeFilter === 'covers'
              ? 'Add a cover with platform: elero to your ESPHome YAML, specifying dst_address, src_address, and channel. Then reflash.'
              : deviceTypeFilter === 'lights'
              ? 'Add a light with platform: elero to your ESPHome YAML, specifying dst_address, src_address, and channel. Then reflash.'
              : 'Add blinds or lights to your ESPHome YAML configuration with their addresses and channels, then reflash. Use the Discovered filter to find addresses from nearby remotes.'
          }
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
