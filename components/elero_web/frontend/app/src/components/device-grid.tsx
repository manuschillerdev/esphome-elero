import { useMemo } from 'preact/hooks'
import { BlindCard } from './blind-card'
import { LightCard } from './light-card'
import { DiscoveredCard } from './discovered-card'
import { useStore } from '@/store'

export function DeviceGrid() {
  const viewMode = useStore((s) => s.viewMode)
  const filter = useStore((s) => s.filter)
  const deviceTypeFilter = useStore((s) => s.deviceTypeFilter)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)
  const states = useStore((s) => s.states)

  const isCompact = viewMode === 'list'

  // Compute discovered addresses (in states but not in config)
  const discovered = useMemo(() => {
    const configuredAddrs = new Set([
      ...blinds.map((b) => b.address),
      ...lights.map((l) => l.address),
    ])
    return Object.keys(states).filter((addr) => !configuredAddrs.has(addr))
  }, [blinds, lights, states])

  // Apply filters
  const showConfigured = filter === 'all' || filter === 'configured'
  const showDiscovered = filter === 'all' || filter === 'discovered'
  const showBlinds = deviceTypeFilter === 'all' || deviceTypeFilter === 'blinds'
  const showLights = deviceTypeFilter === 'all' || deviceTypeFilter === 'lights'

  const filteredBlinds = showConfigured && showBlinds ? blinds : []
  const filteredLights = showConfigured && showLights ? lights : []
  const discoveredAddrs = showDiscovered ? discovered : []

  const hasItems = filteredBlinds.length > 0 || filteredLights.length > 0 || discoveredAddrs.length > 0

  if (!hasItems) {
    return (
      <div className="flex items-center justify-center rounded-xl border border-dashed border-border bg-card py-16 text-sm text-muted-foreground">
        No {filter !== 'all' ? filter : ''} {deviceTypeFilter !== 'all' ? deviceTypeFilter : 'devices'} found
      </div>
    )
  }

  if (isCompact) {
    return (
      <div className="flex flex-col gap-2">
        {filteredBlinds.map((blind) => (
          <BlindCard key={blind.address} blind={blind} compact />
        ))}
        {filteredLights.map((light) => (
          <LightCard key={light.address} light={light} compact />
        ))}
        {discoveredAddrs.map((address) => (
          <DiscoveredCard key={address} address={address} />
        ))}
      </div>
    )
  }

  return (
    <div className="flex flex-col gap-6">
      {/* Configured devices (grid) */}
      {(filteredBlinds.length > 0 || filteredLights.length > 0) && (
        <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
          {filteredBlinds.map((blind) => (
            <BlindCard key={blind.address} blind={blind} />
          ))}
          {filteredLights.map((light) => (
            <LightCard key={light.address} light={light} />
          ))}
        </div>
      )}

      {/* Discovered devices */}
      {discoveredAddrs.length > 0 && (
        <div className="flex flex-col gap-2">
          {(filteredBlinds.length > 0 || filteredLights.length > 0) && (
            <h3 className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
              Discovered
            </h3>
          )}
          <div className="flex flex-col gap-2">
            {discoveredAddrs.map((address) => (
              <DiscoveredCard key={address} address={address} />
            ))}
          </div>
        </div>
      )}
    </div>
  )
}
