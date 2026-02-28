import { Cpu, Clock } from './icons'

export function DashboardHeader() {
  return (
    <header className="flex items-center justify-between">
      <div className="flex items-center gap-3.5">
        <div className="flex size-10 items-center justify-center rounded-lg bg-primary text-primary-foreground">
          <Cpu className="size-5" />
        </div>
        <div className="flex flex-col">
          <h1 className="text-base font-semibold tracking-tight text-foreground">
            lilygo-t-embed
          </h1>
          <p className="text-xs text-muted-foreground">ESP32 Blinds Controller</p>
        </div>
      </div>

      <div className="flex items-center gap-2 rounded-lg border border-border bg-card px-3 py-2 text-xs text-muted-foreground">
        <Clock className="size-3.5" />
        <span className="font-mono tabular-nums">44h 58m 52s</span>
      </div>
    </header>
  )
}
