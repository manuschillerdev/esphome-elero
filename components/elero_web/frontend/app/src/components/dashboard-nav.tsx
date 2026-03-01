import { Badge } from './ui/badge'
import { cn } from '@/lib/utils'
import { Monitor, Radio, FileText, Settings } from './icons'
import { useStore } from '@/store'

export function DashboardNav() {
  const activeTab = useStore((s) => s.activeTab)
  const blinds = useStore((s) => s.config.blinds)
  const rfPackets = useStore((s) => s.rfPackets)
  const logs = useStore((s) => s.logs)

  const tabs = [
    { id: 'devices' as const, label: 'Devices', icon: Monitor, count: blinds.length },
    { id: 'packets' as const, label: 'RF Packets', icon: Radio, count: rfPackets.length },
    { id: 'logs' as const, label: 'Logs', icon: FileText, count: logs.length },
    { id: 'hub' as const, label: 'Hub', icon: Settings },
  ]

  return (
    <nav className="flex items-center gap-1" role="tablist">
      {tabs.map((tab) => (
        <button
          key={tab.id}
          role="tab"
          aria-selected={activeTab === tab.id}
          onClick={() => useStore.getState().setActiveTab(tab.id)}
          className={cn(
            'relative flex items-center gap-2 rounded-lg px-3.5 py-2 text-sm font-medium transition-colors',
            activeTab === tab.id
              ? 'bg-primary/10 text-primary'
              : 'text-muted-foreground hover:bg-accent hover:text-accent-foreground'
          )}
        >
          <tab.icon className="size-4" />
          <span>{tab.label}</span>
          {tab.count !== undefined && (
            <Badge
              variant={activeTab === tab.id ? 'default' : 'secondary'}
              className={cn(
                'ml-0.5 h-5 min-w-5 px-1.5 text-[10px] font-semibold',
                activeTab === tab.id && 'bg-primary text-primary-foreground'
              )}
            >
              {tab.count}
            </Badge>
          )}
        </button>
      ))}
    </nav>
  )
}
