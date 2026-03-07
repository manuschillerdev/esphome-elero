import { useStore } from '@/store'
import { Cpu, RotateCcw } from './icons'
import { Button } from './ui/button'
import { cn } from '@/lib/utils'
import { sendCheckAll } from '@/ws'

export function DashboardHeader() {
  const connected = useStore((s) => s.connected)
  const device = useStore((s) => s.config.device)

  return (
    <header className="flex items-center justify-between">
      <div className="flex items-center gap-3.5">
        <div className="flex size-10 items-center justify-center rounded-lg bg-primary text-primary-foreground">
          <Cpu className="size-5" />
        </div>
        <div className="flex flex-col">
          <h1 className="text-base font-semibold tracking-tight text-foreground">
            {device || 'Elero RF Bridge'}
          </h1>
          <p className="text-xs text-muted-foreground">ESP32 Blinds Controller</p>
        </div>
      </div>

      <div className="flex items-center gap-3">
        <Button
          variant="outline"
          size="icon"
          className="size-8"
          onClick={sendCheckAll}
          disabled={!connected}
          title="Check all devices"
        >
          <RotateCcw className="size-3.5" />
        </Button>
        <div className="flex items-center gap-2 rounded-lg border border-border bg-card px-3 py-2">
          <div className={cn(
            'size-2.5 rounded-full',
            connected ? 'bg-green-500' : 'bg-red-500'
          )} />
          <span className="text-xs text-muted-foreground">
            {connected ? 'Connected' : 'Disconnected'}
          </span>
        </div>
      </div>
    </header>
  )
}
