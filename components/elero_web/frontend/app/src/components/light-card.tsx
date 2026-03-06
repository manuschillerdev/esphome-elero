import { Card } from './ui/card'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { InlineEdit, InlineEditNumber } from './ui/inline-edit'
import { SignalIndicator } from './signal-indicator'
import { Lightbulb, LightbulbOff } from './icons'
import { cn } from '@/lib/utils'
import { useStore, type LightConfig, getStateLabel } from '@/store'
import { sendCommand } from '@/ws'

interface LightCardProps {
  light: LightConfig
  compact?: boolean
}

export function LightCard({ light, compact }: LightCardProps) {
  const state = useStore((s) => s.states[light.address])
  const updateLight = useStore((s) => s.updateLight)
  const stateLabel = getStateLabel(state?.state)
  const isOn = stateLabel === 'ON'

  if (compact) {
    return (
      <div className="group flex items-center gap-4 rounded-xl border bg-card px-4 py-3 transition-all hover:shadow-sm">
        {/* Name + meta column */}
        <div className="flex min-w-0 flex-col gap-0.5" style={{ width: '220px', flexShrink: 0 }}>
          <div className="flex items-center gap-2">
            <span className="text-sm font-semibold text-foreground truncate">
              <InlineEdit
                value={light.name}
                onSave={(name) => updateLight(light.address, { name })}
              />
            </span>
          </div>
          <div className="flex items-center gap-2 text-[11px] text-muted-foreground">
            <Badge
              variant="secondary"
              className="font-mono text-[9px] tracking-wider text-muted-foreground px-1 py-0"
            >
              {light.address}
            </Badge>
            <span>CH {light.channel}</span>
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
              isOn && 'bg-warning/20 text-warning-foreground'
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
                variant={isOn ? 'default' : 'outline'}
                size="icon"
                className="size-8"
                onClick={() => sendCommand(light.address, 'up')}
              >
                <Lightbulb className="size-3.5" />
                <span className="sr-only">On</span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>Turn On</TooltipContent>
          </Tooltip>
          <Tooltip>
            <TooltipTrigger>
              <Button
                variant={!isOn && state ? 'secondary' : 'outline'}
                size="icon"
                className="size-8"
                onClick={() => sendCommand(light.address, 'down')}
              >
                <LightbulbOff className="size-3.5" />
                <span className="sr-only">Off</span>
              </Button>
            </TooltipTrigger>
            <TooltipContent>Turn Off</TooltipContent>
          </Tooltip>
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
              <InlineEdit
                value={light.name}
                onSave={(name) => updateLight(light.address, { name })}
              />
            </h3>
            <Badge
              variant="secondary"
              className="shrink-0 font-mono text-[10px] tracking-wider text-muted-foreground"
            >
              {light.address}
            </Badge>
          </div>
          <div className="flex flex-wrap items-center gap-x-3 gap-y-1 text-xs text-muted-foreground">
            <span className="flex items-center gap-1.5 font-medium">
              <span className="text-card-foreground">CH {light.channel}</span>
            </span>
            {light.remote && (
              <span className="flex items-center gap-1">
                <span className="text-muted-foreground/70">Remote:</span>
                <span className="font-mono text-[10px]">{light.remote}</span>
              </span>
            )}
            {state && (
              <span className="flex items-center gap-1.5">
                <SignalIndicator rssi={state.rssi ?? -100} />
                <span>{state.rssi?.toFixed(0) ?? '-'} dBm</span>
              </span>
            )}
          </div>
          {/* Dim duration config */}
          <div className="flex items-center gap-2 text-[10px] text-muted-foreground/70">
            <span>
              dim:{' '}
              <InlineEditNumber
                value={Math.round((light.dim_ms || 0) / 1000)}
                onSave={(s) => updateLight(light.address, { dim_ms: s * 1000 })}
                suffix="s"
                min={0}
                max={60}
              />
            </span>
          </div>
        </div>
      </div>

      {/* State display */}
      <div className="px-5 pt-5 pb-4">
        <div className="flex items-center gap-3">
          <div
            className={cn(
              'flex size-12 items-center justify-center rounded-full transition-colors',
              isOn ? 'bg-warning/20 text-warning-foreground' : 'bg-muted text-muted-foreground'
            )}
          >
            {isOn ? <Lightbulb className="size-6" /> : <LightbulbOff className="size-6" />}
          </div>
          <Badge
            variant="secondary"
            className={cn(
              'text-sm px-3 py-1',
              isOn && 'bg-warning/20 text-warning-foreground'
            )}
          >
            {stateLabel}
          </Badge>
        </div>
      </div>

      {/* Separator */}
      <div className="mx-5 border-t border-border" />

      {/* Action buttons */}
      <div className="grid grid-cols-2 gap-2 p-4 px-5">
        <Button
          variant={isOn ? 'default' : 'outline'}
          size="sm"
          className="gap-1.5 text-xs font-medium"
          onClick={() => sendCommand(light.address, 'up')}
        >
          <Lightbulb className="size-3.5" />
          On
        </Button>
        <Button
          variant={!isOn && state ? 'secondary' : 'outline'}
          size="sm"
          className="gap-1.5 text-xs font-medium"
          onClick={() => sendCommand(light.address, 'down')}
        >
          <LightbulbOff className="size-3.5" />
          Off
        </Button>
      </div>
    </Card>
  )
}
