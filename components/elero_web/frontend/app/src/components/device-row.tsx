import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { InlineEdit, InlineEditNumber } from './ui/inline-edit'
import { SignalIndicator } from './signal-indicator'
import { ChevronUp, Square, ChevronDown, Shrink, Lightbulb, LightbulbOff } from './icons'
import { cn } from '@/lib/utils'
import { useStore, getStateLabel, isMovingState, type BlindConfig, type LightConfig } from '@/store'
import { sendCommand } from '@/ws'

// ─── Shared cell renderers (used by DataTable column definitions) ───────────

export function DeviceCell({ blind }: { blind: BlindConfig }) {
  const updateBlind = useStore((s) => s.updateBlind)
  return (
    <div className="flex min-w-0 flex-col gap-0.5">
      <span className="truncate text-sm font-medium text-foreground">
        <InlineEdit
          value={blind.name}
          onSave={(name) => updateBlind(blind.address, { name })}
        />
      </span>
      <div className="flex items-center gap-2 text-[10px] text-muted-foreground">
        <span className="font-mono">{blind.address}</span>
        <span>CH {blind.channel}</span>
        <span className="flex items-center gap-0.5 text-muted-foreground/70">
          <span>&#x25B3;</span>
          <InlineEditNumber
            value={+(blind.open_ms / 1000).toFixed(1)}
            onSave={(v) => updateBlind(blind.address, { open_ms: Math.round(v * 1000) })}
            suffix="s"
            min={0}
            max={300}
            step={0.1}
            className="text-muted-foreground/70"
          />
        </span>
        <span className="flex items-center gap-0.5 text-muted-foreground/70">
          <span>&#x25BD;</span>
          <InlineEditNumber
            value={+(blind.close_ms / 1000).toFixed(1)}
            onSave={(v) => updateBlind(blind.address, { close_ms: Math.round(v * 1000) })}
            suffix="s"
            min={0}
            max={300}
            step={0.1}
            className="text-muted-foreground/70"
          />
        </span>
      </div>
    </div>
  )
}

export function LightDeviceCell({ light }: { light: LightConfig }) {
  const updateLight = useStore((s) => s.updateLight)
  return (
    <div className="flex min-w-0 flex-col gap-0.5">
      <span className="truncate text-sm font-medium text-foreground">
        <InlineEdit
          value={light.name}
          onSave={(name) => updateLight(light.address, { name })}
        />
      </span>
      <div className="flex items-center gap-2 text-[10px] text-muted-foreground">
        <span className="font-mono">{light.address}</span>
        <span>CH {light.channel}</span>
      </div>
    </div>
  )
}

export function StateCell({ address }: { address: string }) {
  const pkt = useStore((s) => s.states[address])
  const stateLabel = getStateLabel(pkt?.state)
  const moving = isMovingState(pkt?.state)
  return (
    <Badge
      variant="secondary"
      className={cn(
        'text-[10px] px-2 py-0.5',
        moving && 'bg-warning/10 text-warning-foreground animate-pulse',
      )}
    >
      {stateLabel}
    </Badge>
  )
}

export function SignalCell({ address }: { address: string }) {
  const pkt = useStore((s) => s.states[address])
  if (!pkt) return null
  return (
    <div className="flex items-center gap-1.5 text-[10px] text-muted-foreground">
      <SignalIndicator rssi={pkt.rssi ?? -100} />
      <span>{pkt.rssi?.toFixed(0)} dBm</span>
    </div>
  )
}

export function BlindActions({ blind }: { blind: BlindConfig }) {
  return (
    <div className="flex items-center justify-end gap-1 text-primary">
      <div className="size-7">
        {blind.tilt && (
          <Tooltip>
            <TooltipTrigger>
              <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendCommand(blind.address, 'tilt')}>
                <Shrink className="size-3.5" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Tilt</TooltipContent>
          </Tooltip>
        )}
      </div>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendCommand(blind.address, 'up')}>
            <ChevronUp className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Open</TooltipContent>
      </Tooltip>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendCommand(blind.address, 'stop')}>
            <Square className="size-3" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Stop</TooltipContent>
      </Tooltip>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendCommand(blind.address, 'down')}>
            <ChevronDown className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Close</TooltipContent>
      </Tooltip>
    </div>
  )
}

export function LightActions({ light }: { light: LightConfig }) {
  return (
    <div className="flex items-center justify-end gap-1">
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendCommand(light.address, 'up')}>
            <Lightbulb className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>On</TooltipContent>
      </Tooltip>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendCommand(light.address, 'down')}>
            <LightbulbOff className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Off</TooltipContent>
      </Tooltip>
    </div>
  )
}
