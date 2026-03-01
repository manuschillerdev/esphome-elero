import { Card } from './ui/card'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { SignalIndicator } from './signal-indicator'
import { Copy } from './icons'
import { useStore } from '@/store'

function formatTime(ms: number): string {
  if (!ms) return ''
  const s = Math.floor(ms / 1000)
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}

function copyYaml(address: string, state: { ch?: number; dst?: string }) {
  const yaml = `  - platform: elero
    blind_address: ${address}
    channel: ${state?.ch || 0}
    remote_address: ${state?.dst || '0x000000'}
    name: "New Blind"
    # open_duration: 25s
    # close_duration: 22s`
  navigator.clipboard.writeText(yaml)
}

export function DiscoveredCard({ address }: { address: string }) {
  const state = useStore((s) => s.states[address])

  return (
    <Card className="gap-0 overflow-hidden border-dashed border-warning/40 bg-warning/5 p-0">
      <div className="flex items-center justify-between p-4">
        <div className="flex flex-col gap-1">
          <div className="flex items-center gap-2">
            <span className="relative flex size-2">
              <span className="absolute inline-flex size-full animate-ping rounded-full bg-warning opacity-75" />
              <span className="relative inline-flex size-2 rounded-full bg-warning" />
            </span>
            <span className="font-mono text-sm font-medium">{address}</span>
            {state?.ch && (
              <Badge variant="secondary" className="font-mono text-[10px]">
                CH {state.ch}
              </Badge>
            )}
          </div>
          <div className="flex items-center gap-3 text-xs text-muted-foreground">
            {state && (
              <>
                <span>Last seen: {formatTime(state.t)}</span>
                <span className="flex items-center gap-1.5">
                  <SignalIndicator rssi={state.rssi ?? -100} />
                  <span>{state.rssi?.toFixed(1) ?? '-'} dBm</span>
                </span>
              </>
            )}
          </div>
        </div>
        <Button
          variant="outline"
          size="sm"
          className="gap-1.5 text-xs"
          onClick={() => copyYaml(address, state || {})}
        >
          <Copy className="size-3" />
          Copy YAML
        </Button>
      </div>
    </Card>
  )
}
