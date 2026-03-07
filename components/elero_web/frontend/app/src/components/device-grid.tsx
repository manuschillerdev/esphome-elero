import { RemoteGroup } from './remote-group'
import { DiscoveredCard } from './discovered-card'
import { useStore, buildRemoteGroups, buildDiscoveredAddresses } from '@/store'

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
    </div>
  )
}
