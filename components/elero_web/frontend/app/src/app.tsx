import { effect } from '@preact/signals'
import { activeTab as activeTabSignal, setActiveTab } from './store'
import { initWs } from './ws'
import { DashboardHeader } from './components/dashboard-header'
import { DashboardNav } from './components/dashboard-nav'
import { ManageTab } from './components/manage-tab'
import { RfPackets } from './components/rf-packets'
import { HubPanel } from './components/hub-panel'
import { Toaster } from './components/toaster'

// ─── Side effects (module-level, run once on import) ────────────────────────

const VALID_TABS = new Set(['manage', 'packets', 'hub'] as const)
type Tab = 'manage' | 'packets' | 'hub'

function tabFromHash(): Tab | null {
  const h = location.hash.replace('#', '')
  if (!h) return 'manage'
  return VALID_TABS.has(h as Tab) ? (h as Tab) : null
}

// On load: hash → store
const initial = tabFromHash()
if (initial) setActiveTab(initial)

// On hashchange: hash → store
window.addEventListener('hashchange', () => {
  const tab = tabFromHash()
  if (tab) setActiveTab(tab)
})

// On signal change: store → hash
effect(() => {
  const tab = activeTabSignal.value
  const hash = tab === 'manage' ? '' : `#${tab}`
  if (location.hash !== hash) {
    history.replaceState(null, '', hash || location.pathname)
  }
})

// WebSocket: connect once, auto-reconnect handled internally
initWs()

// ─── App Component ──────────────────────────────────────────────────────────

export function App() {
  const activeTab = activeTabSignal.value

  return (
    <div className="min-h-screen bg-background">
      <div className="mx-auto max-w-5xl px-4 py-6 sm:px-6 lg:px-8">
        <div className="flex flex-col gap-6">
          <DashboardHeader />

          <div className="flex flex-col gap-5">
            <DashboardNav />
            <div className="border-t border-border" />

            {activeTab === 'manage' && <ManageTab />}
            {activeTab === 'packets' && <RfPackets />}
            {activeTab === 'hub' && <HubPanel />}
          </div>
        </div>
      </div>
      <Toaster />
    </div>
  )
}
