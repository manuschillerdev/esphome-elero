import { RemoteGroup } from './remote-group'
import { DiscoveredCard } from './discovered-card'
import { useStore, buildRemoteGroups, buildDiscoveredAddresses } from '@/store'

function EmptyState({ title, description }: { title: string; description: string }) {
  return (
    <div className="rounded-xl border border-dashed border-border bg-muted/20 px-6 py-10 text-center">
      <p className="text-sm font-medium text-foreground">{title}</p>
      <p className="mt-1 text-xs text-muted-foreground">{description}</p>
    </div>
  )
}

export function DeviceGrid() {
  const filter = useStore((s) => s.filter)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)
  const states = useStore((s) => s.states)
  const deviceTypeFilter = useStore((s) => s.deviceTypeFilter)
  const remoteGroups = buildRemoteGroups(blinds, lights, deviceTypeFilter)
  const discovered = buildDiscoveredAddresses(blinds, lights, states)

  const showConfigured = filter === 'all' || filter === 'configured'
  const showDiscovered = filter === 'all' || filter === 'discovered'

  const hasConfiguredContent = showConfigured && remoteGroups.length > 0
  const hasDiscoveredContent = showDiscovered && discovered.length > 0
  const hasContent = hasConfiguredContent || hasDiscoveredContent

  return (
    <div className="flex flex-col gap-4">
      {showDiscovered && discovered.length > 0 && (
        <div className="flex flex-col gap-2">
          {discovered.map((address) => (
            <DiscoveredCard key={address} address={address} />
          ))}
        </div>
      )}

      {showConfigured && remoteGroups.map((group) => (
        <RemoteGroup
          key={group.address}
          address={group.address}
          blinds={group.blinds}
          lights={group.lights}
        />
      ))}

      {!hasContent && filter === 'discovered' && (
        <EmptyState
          title="No devices discovered yet"
          description="Press buttons on your physical Elero remotes — new RF addresses will appear here automatically. If nothing shows up, check your frequency and pin configuration on the Hub page."
        />
      )}

      {!hasContent && filter === 'configured' && (
        <EmptyState
          title={
            deviceTypeFilter === 'blinds' ? 'No blinds configured'
            : deviceTypeFilter === 'lights' ? 'No lights configured'
            : 'No devices configured'
          }
          description={
            deviceTypeFilter === 'blinds'
              ? 'Add a cover with platform: elero to your ESPHome YAML, specifying dst_address, src_address, and channel. Then reflash.'
              : deviceTypeFilter === 'lights'
              ? 'Add a light with platform: elero to your ESPHome YAML, specifying dst_address, src_address, and channel. Then reflash.'
              : 'Add blinds or lights to your ESPHome YAML configuration with their addresses and channels, then reflash. Use the Discovered filter to find addresses from nearby remotes.'
          }
        />
      )}

      {!hasContent && filter === 'all' && (
        <EmptyState
          title={
            deviceTypeFilter === 'blinds' ? 'No blinds configured'
            : deviceTypeFilter === 'lights' ? 'No lights configured'
            : 'No devices yet'
          }
          description={
            deviceTypeFilter === 'blinds'
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
