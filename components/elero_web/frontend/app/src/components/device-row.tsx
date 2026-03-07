import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { InlineEdit, InlineEditNumber } from './ui/inline-edit'
import { SignalIndicator } from './signal-indicator'
import { ChevronUp, Square, ChevronDown, Shrink, Lightbulb, LightbulbOff, Save } from './icons'
import { cn } from '@/lib/utils'
import { updateDevice, getStateLabel, isMovingState, type Device } from '@/store'
import { sendDeviceCommand, sendUpsertDevice } from '@/ws'

// ─── Shared cell renderers (used by DataTable column definitions) ───────────

export function DeviceCell({ device }: { device: Device }) {
  return (
    <div className="flex min-w-0 flex-col gap-0.5">
      <span className="truncate text-sm font-medium text-foreground">
        <InlineEdit
          value={device.name}
          onSave={(name) => updateDevice(device.address, { name })}
        />
      </span>
      <div className="flex items-center gap-2 text-[10px] text-muted-foreground">
        <span className="font-mono">{device.address}</span>
        <span>CH {device.channel}</span>
        <span className="flex items-center gap-0.5 text-muted-foreground/70">
          <span>&#x25B3;</span>
          <InlineEditNumber
            value={+(device.open_ms / 1000).toFixed(1)}
            onSave={(v) => updateDevice(device.address, { open_ms: Math.round(v * 1000) })}
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
            value={+(device.close_ms / 1000).toFixed(1)}
            onSave={(v) => updateDevice(device.address, { close_ms: Math.round(v * 1000) })}
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

export function LightDeviceCell({ device }: { device: Device }) {
  return (
    <div className="flex min-w-0 flex-col gap-0.5">
      <span className="truncate text-sm font-medium text-foreground">
        <InlineEdit
          value={device.name}
          onSave={(name) => updateDevice(device.address, { name })}
        />
      </span>
      <div className="flex items-center gap-2 text-[10px] text-muted-foreground">
        <span className="font-mono">{device.address}</span>
        <span>CH {device.channel}</span>
      </div>
    </div>
  )
}

export function StateCell({ device }: { device: Device }) {
  const stateLabel = getStateLabel(device.lastStatus?.state)
  const moving = isMovingState(device.lastStatus?.state)
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

export function SignalCell({ device }: { device: Device }) {
  if (!device.lastStatus) return null
  return (
    <div className="flex items-center gap-1.5 text-[10px] text-muted-foreground">
      <SignalIndicator rssi={device.lastStatus.rssi ?? -100} />
      <span>{device.lastStatus.rssi?.toFixed(0)} dBm</span>
    </div>
  )
}

export function BlindActions({ device, showSave }: { device: Device; showSave?: boolean }) {
  return (
    <div className="flex items-center justify-end gap-1 text-primary">
      {showSave && (
        <>
          <Tooltip>
            <TooltipTrigger>
              <Button
                variant="ghost"
                size="icon"
                className="size-7 text-success hover:text-success"
                onClick={() => sendUpsertDevice({ address: device.address, remote: device.remote, channel: device.channel, name: device.name, device_type: 'cover' })}
              >
                <Save className="size-3.5" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Save to NVS</TooltipContent>
          </Tooltip>
          <div className="mx-0.5 h-4 w-px bg-border" />
        </>
      )}
      <div className="size-7">
        {device.tilt && (
          <Tooltip>
            <TooltipTrigger>
              <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendDeviceCommand(device, 'tilt')}>
                <Shrink className="size-3.5" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Tilt</TooltipContent>
          </Tooltip>
        )}
      </div>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendDeviceCommand(device, 'up')}>
            <ChevronUp className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Open</TooltipContent>
      </Tooltip>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendDeviceCommand(device, 'stop')}>
            <Square className="size-3" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Stop</TooltipContent>
      </Tooltip>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendDeviceCommand(device, 'down')}>
            <ChevronDown className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Close</TooltipContent>
      </Tooltip>
    </div>
  )
}

export function LightActions({ device, showSave }: { device: Device; showSave?: boolean }) {
  return (
    <div className="flex items-center justify-end gap-1">
      {showSave && (
        <>
          <Tooltip>
            <TooltipTrigger>
              <Button
                variant="ghost"
                size="icon"
                className="size-7 text-success hover:text-success"
                onClick={() => sendUpsertDevice({ address: device.address, remote: device.remote, channel: device.channel, name: device.name, device_type: 'light' })}
              >
                <Save className="size-3.5" />
              </Button>
            </TooltipTrigger>
            <TooltipContent>Save to NVS</TooltipContent>
          </Tooltip>
          <div className="mx-0.5 h-4 w-px bg-border" />
        </>
      )}
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendDeviceCommand(device, 'up')}>
            <Lightbulb className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>On</TooltipContent>
      </Tooltip>
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary" onClick={() => sendDeviceCommand(device, 'down')}>
            <LightbulbOff className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>Off</TooltipContent>
      </Tooltip>
    </div>
  )
}
