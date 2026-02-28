import { Badge } from './ui/badge'
import { cn } from '@/lib/utils'
import { Monitor, Settings } from './icons'
import { useStore } from '@/store'

export function DashboardNav() {
  const activeTab = useStore((s) => s.activeTab)
  const setActiveTab = useStore((s) => s.setActiveTab)
  const devices = useStore((s) => s.devices)
  const configuredCount = devices.filter((d) => d.status === 'configured').length

  return (
    <nav className="flex items-center gap-1" role="tablist">
      <button
        role="tab"
        aria-selected={activeTab === 'devices'}
        onClick={() => setActiveTab('devices')}
        className={cn(
          'relative flex items-center gap-2 rounded-lg px-3.5 py-2 text-sm font-medium transition-colors',
          activeTab === 'devices'
            ? 'bg-primary/10 text-primary'
            : 'text-muted-foreground hover:bg-accent hover:text-accent-foreground'
        )}
      >
        <Monitor className="size-4" />
        <span>Devices</span>
        <Badge
          variant={activeTab === 'devices' ? 'default' : 'secondary'}
          className={cn(
            'ml-0.5 h-5 min-w-5 px-1.5 text-[10px] font-semibold',
            activeTab === 'devices' && 'bg-primary text-primary-foreground'
          )}
        >
          {configuredCount}
        </Badge>
      </button>
      <button
        role="tab"
        aria-selected={activeTab === 'hub'}
        onClick={() => setActiveTab('hub')}
        className={cn(
          'relative flex items-center gap-2 rounded-lg px-3.5 py-2 text-sm font-medium transition-colors',
          activeTab === 'hub'
            ? 'bg-primary/10 text-primary'
            : 'text-muted-foreground hover:bg-accent hover:text-accent-foreground'
        )}
      >
        <Settings className="size-4" />
        <span>Hub</span>
      </button>
    </nav>
  )
}
