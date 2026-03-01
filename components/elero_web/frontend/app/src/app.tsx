import { useEffect } from 'preact/hooks'
import { useStore } from './store'
import { connect, disconnect } from './ws'
import { DashboardHeader } from './components/dashboard-header'
import { DashboardNav } from './components/dashboard-nav'
import { ControlBar } from './components/control-bar'
import { DeviceGrid } from './components/device-grid'
import { RfPackets } from './components/rf-packets'
import { LogsPanel } from './components/logs-panel'
import { HubPanel } from './components/hub-panel'

export function App() {
  const activeTab = useStore((s) => s.activeTab)

  useEffect(() => {
    connect()
    return () => disconnect()
  }, [])

  return (
    <div className="min-h-screen bg-background">
      <div className="mx-auto max-w-5xl px-4 py-6 sm:px-6 lg:px-8">
        <div className="flex flex-col gap-6">
          <DashboardHeader />

          <div className="flex flex-col gap-5">
            <DashboardNav />
            <div className="border-t border-border" />

            {activeTab === 'devices' && (
              <div className="flex flex-col gap-4">
                <ControlBar />
                <DeviceGrid />
              </div>
            )}

            {activeTab === 'packets' && <RfPackets />}
            {activeTab === 'logs' && <LogsPanel />}
            {activeTab === 'hub' && <HubPanel />}
          </div>
        </div>
      </div>
    </div>
  )
}
