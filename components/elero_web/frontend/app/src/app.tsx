import { useStore } from './store'
import { initWs } from './ws'
import { DashboardHeader } from './components/dashboard-header'
import { DashboardNav } from './components/dashboard-nav'
import { DiscoveryBanner } from './components/discovery-banner'
import { ControlBar } from './components/control-bar'
import { DeviceGrid } from './components/device-grid'
import { RfPackets } from './components/rf-packets'
import { HubPanel } from './components/hub-panel'

// ─── Side effects (module-level, run once on import) ────────────────────────

// URL hash ↔ store sync
const VALID_TABS = new Set(['devices', 'packets', 'hub'] as const)
type Tab = 'devices' | 'packets' | 'hub'

function tabFromHash(): Tab | null {
  const h = location.hash.replace('#', '')
  return VALID_TABS.has(h as Tab) ? (h as Tab) : null
}

// On load: hash → store
const initial = tabFromHash()
if (initial) useStore.getState().setActiveTab(initial)

// On hashchange: hash → store
window.addEventListener('hashchange', () => {
  const tab = tabFromHash()
  if (tab) useStore.getState().setActiveTab(tab)
})

// On store change: store → hash
useStore.subscribe((s, prev) => {
  if (s.activeTab !== prev.activeTab) {
    const hash = s.activeTab === 'devices' ? '' : `#${s.activeTab}`
    if (location.hash !== hash) {
      history.replaceState(null, '', hash || location.pathname)
    }
  }
})

// WebSocket: connect once, auto-reconnect handled internally
initWs()

// ─── App Component ──────────────────────────────────────────────────────────

export function App() {
  const activeTab = useStore((s) => s.activeTab)

  return (
    <div className="min-h-screen bg-background">
      <div className="mx-auto max-w-5xl px-4 py-6 sm:px-6 lg:px-8">
        <div className="flex flex-col gap-6">
          <DashboardHeader />

          <div className="flex flex-col gap-5">
            <DashboardNav />
            <DiscoveryBanner />
            <div className="border-t border-border" />

            {activeTab === 'devices' && (
              <div className="flex flex-col gap-4">
                <ControlBar />
                <DeviceGrid />
              </div>
            )}

            {activeTab === 'packets' && <RfPackets />}
            {activeTab === 'hub' && <HubPanel />}
          </div>
        </div>
      </div>
    </div>
  )
}
