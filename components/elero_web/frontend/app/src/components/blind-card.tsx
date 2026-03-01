import { Card } from './ui/card'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { SignalIndicator } from './signal-indicator'
import { ChevronUp, Square, ChevronDown, Shrink } from './icons'
import { cn } from '@/lib/utils'
import { useStore, type BlindConfig, getStateLabel, isMovingState } from '@/store'
import { sendCommand } from '@/ws'

// Format duration in ms to human-readable string
function formatDuration(ms: number | undefined): string {
  if (!ms || ms === 0) return '-'
  const seconds = Math.round(ms / 1000)
  return `${seconds}s`
}

interface BlindCardProps {
  blind: BlindConfig
  compact?: boolean
}

export function BlindCard({ blind, compact }: BlindCardProps) {
  const state = useStore((s) => s.states[blind.address])
  const stateLabel = getStateLabel(state?.state)
  const isMoving = isMovingState(state?.state)

  if (compact) {
    return (
      <div className="group flex items-center gap-4 rounded-xl border bg-card px-4 py-3 transition-all hover:shadow-sm">
        {/* Name + meta column */}
        <div className="flex min-w-0 flex-col gap-0.5" style={{ width: '220px', flexShrink: 0 }}>
          <div className="flex items-center gap-2">
            <span className="text-sm font-semibold text-foreground truncate">{blind.name}</span>
          </div>
          <div className="flex items-center gap-2 text-[11px] text-muted-foreground">
            <Badge
              variant="secondary"
              className="font-mono text-[9px] tracking-wider text-muted-foreground px-1 py-0"
            >
              {blind.address}
            </Badge>
            <span>CH {blind.channel}</span>
            {state && (
              <span className="flex items-center gap-1">
                <SignalIndicator rssi={state.rssi ?? -100} />
                {state.rssi?.toFixed(0) ?? '-'} dBm
              </span>
            )}
          </div>
        </div>

        {/* State display */}
        <div className="flex flex-1 items-center gap-3">
          <Badge
            variant="secondary"
            className={cn(
              'text-xs px-2 py-0.5',
              stateLabel === 'TOP' && 'bg-success/10 text-success',
              stateLabel === 'BOTTOM' && 'bg-muted text-muted-foreground',
              isMoving && 'bg-primary/10 text-primary'
            )}
          >
            {stateLabel}
          </Badge>
        </div>

        {/* Actions */}
        <div className="flex items-center gap-1 shrink-0">
          <Tooltip>
            <TooltipTrigger>
              <Button
                variant="outline"
                size="icon"
                className="size-8"
                onClick={() => sendCommand(blind.address, 'up')}
              >
                <ChevronUp className="size-3.5" />
                <span className="sr-only">Open</span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>Open</TooltipContent>
          </Tooltip>
          <Tooltip>
            <TooltipTrigger>
              <Button
                variant="outline"
                size="icon"
                className="size-8"
                onClick={() => sendCommand(blind.address, 'stop')}
              >
                <Square className="size-3" />
                <span className="sr-only">Stop</span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>Stop</TooltipContent>
          </Tooltip>
          <Tooltip>
            <TooltipTrigger>
              <Button
                variant="outline"
                size="icon"
                className="size-8"
                onClick={() => sendCommand(blind.address, 'down')}
              >
                <ChevronDown className="size-3.5" />
                <span className="sr-only">Close</span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>Close</TooltipContent>
          </Tooltip>
          {blind.tilt && (
            <Tooltip>
              <TooltipTrigger>
                <Button
                  variant="outline"
                  size="icon"
                  className="size-8"
                  onClick={() => sendCommand(blind.address, 'tilt')}
                >
                  <Shrink className="size-3.5" />
                  <span className="sr-only">Tilt</span>
                </Button>
              </TooltipTrigger>
              <TooltipContent>Tilt</TooltipContent>
            </Tooltip>
          )}
        </div>
      </div>
    )
  }

  return (
    <Card className="group relative gap-0 overflow-hidden p-0 transition-all hover:shadow-md">
      {/* Header zone */}
      <div className="flex items-start justify-between p-5 pb-0">
        <div className="flex flex-col gap-1.5 min-w-0 flex-1">
          <div className="flex items-center gap-2.5">
            <h3 className="text-base font-semibold tracking-tight text-card-foreground">
              {blind.name}
            </h3>
            <Badge
              variant="secondary"
              className="shrink-0 font-mono text-[10px] tracking-wider text-muted-foreground"
            >
              {blind.address}
            </Badge>
          </div>
          <div className="flex flex-wrap items-center gap-x-3 gap-y-1 text-xs text-muted-foreground">
            <span className="flex items-center gap-1.5 font-medium">
              <span className="text-card-foreground">CH {blind.channel}</span>
            </span>
            {blind.remote && (
              <span className="flex items-center gap-1">
                <span className="text-muted-foreground/70">Remote:</span>
                <span className="font-mono text-[10px]">{blind.remote}</span>
              </span>
            )}
            {state && (
              <span className="flex items-center gap-1.5">
                <SignalIndicator rssi={state.rssi ?? -100} />
                <span>{state.rssi?.toFixed(0) ?? '-'} dBm</span>
              </span>
            )}
          </div>
          {/* Movement duration config */}
          {(blind.open_ms > 0 || blind.close_ms > 0) && (
            <div className="flex items-center gap-2 text-[10px] text-muted-foreground/70">
              <span>open: {formatDuration(blind.open_ms)}</span>
              <span>|</span>
              <span>close: {formatDuration(blind.close_ms)}</span>
            </div>
          )}
        </div>
      </div>

      {/* State display */}
      <div className="px-5 pt-5 pb-4">
        <div className="flex items-baseline gap-2 pb-1">
          <Badge
            variant="secondary"
            className={cn(
              'text-sm px-3 py-1',
              stateLabel === 'TOP' && 'bg-success/10 text-success',
              stateLabel === 'BOTTOM' && 'bg-muted text-muted-foreground',
              isMoving && 'bg-primary/10 text-primary animate-pulse'
            )}
          >
            {stateLabel}
          </Badge>
        </div>
      </div>

      {/* Separator */}
      <div className="mx-5 border-t border-border" />

      {/* Action buttons */}
      <div className="grid grid-cols-4 gap-2 p-4 px-5">
        <Button
          variant="outline"
          size="sm"
          className="gap-1.5 text-xs font-medium"
          onClick={() => sendCommand(blind.address, 'up')}
        >
          <ChevronUp className="size-3.5" />
          Open
        </Button>
        <Button
          variant="outline"
          size="sm"
          className="gap-1.5 text-xs font-medium"
          onClick={() => sendCommand(blind.address, 'stop')}
        >
          <Square className="size-3" />
          Stop
        </Button>
        <Button
          variant="outline"
          size="sm"
          className="gap-1.5 text-xs font-medium"
          onClick={() => sendCommand(blind.address, 'down')}
        >
          <ChevronDown className="size-3.5" />
          Close
        </Button>
        {blind.tilt ? (
          <Button
            variant="outline"
            size="sm"
            className="gap-1.5 text-xs font-medium"
            onClick={() => sendCommand(blind.address, 'tilt')}
          >
            <Shrink className="size-3.5" />
            Tilt
          </Button>
        ) : (
          <div />
        )}
      </div>
    </Card>
  )
}
