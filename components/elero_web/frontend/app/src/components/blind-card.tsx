import { useState, useRef, useEffect } from 'preact/compat'
import { Card } from './ui/card'
import { Button } from './ui/button'
import { Slider } from './ui/slider'
import { Badge } from './ui/badge'
import { Switch } from './ui/switch'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { SignalIndicator } from './signal-indicator'
import {
  ChevronUp,
  Square,
  ChevronDown,
  Settings,
  Shrink,
  Plus,
  Pencil,
  Timer,
} from './icons'
import { cn } from '@/lib/utils'
import { useStore, type BlindDevice } from '@/store'

function EditableName({ value, address }: { value: string; address: string }) {
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState(value)
  const inputRef = useRef<HTMLInputElement>(null)
  const renameDevice = useStore((s) => s.renameDevice)

  useEffect(() => {
    if (editing) {
      inputRef.current?.focus()
      inputRef.current?.select()
    }
  }, [editing])

  const commit = () => {
    const trimmed = draft.trim()
    if (trimmed && trimmed !== value) {
      renameDevice(address, trimmed)
    } else {
      setDraft(value)
    }
    setEditing(false)
  }

  if (editing) {
    return (
      <input
        ref={inputRef}
        value={draft}
        onInput={(e) => setDraft((e.target as HTMLInputElement).value)}
        onBlur={commit}
        onKeyDown={(e) => {
          if (e.key === 'Enter') commit()
          if (e.key === 'Escape') {
            setDraft(value)
            setEditing(false)
          }
        }}
        className="h-7 w-full min-w-0 rounded-md border border-input bg-background px-2 text-base font-semibold tracking-tight text-card-foreground outline-none focus:ring-2 focus:ring-ring"
      />
    )
  }

  return (
    <button
      onClick={() => setEditing(true)}
      className="group/name flex items-center gap-1.5 rounded-md text-left transition-colors hover:bg-accent/50 -ml-1 px-1 py-0.5"
    >
      <h3 className="text-base font-semibold tracking-tight text-card-foreground">{value}</h3>
      <Pencil className="size-3 text-muted-foreground opacity-0 transition-opacity group-hover/name:opacity-100" />
    </button>
  )
}

function EditableDuration({
  label,
  value,
  address,
  field,
}: {
  label: string
  value: number
  address: string
  field: 'durationUp' | 'durationDown'
}) {
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState(String(value))
  const inputRef = useRef<HTMLInputElement>(null)
  const updateDeviceDuration = useStore((s) => s.updateDeviceDuration)

  useEffect(() => {
    if (editing) {
      inputRef.current?.focus()
      inputRef.current?.select()
    }
  }, [editing])

  const commit = () => {
    const num = parseFloat(draft)
    if (!isNaN(num) && num > 0 && num !== value) {
      updateDeviceDuration(address, field, num)
    } else {
      setDraft(String(value))
    }
    setEditing(false)
  }

  if (editing) {
    return (
      <span className="inline-flex items-center gap-0.5">
        <input
          ref={inputRef}
          value={draft}
          onInput={(e) => setDraft((e.target as HTMLInputElement).value)}
          onBlur={commit}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commit()
            if (e.key === 'Escape') {
              setDraft(String(value))
              setEditing(false)
            }
          }}
          className="h-5 w-10 rounded border border-input bg-background px-1 text-center text-[11px] font-medium tabular-nums text-card-foreground outline-none focus:ring-2 focus:ring-ring"
        />
        <span className="text-[11px] text-muted-foreground">s</span>
      </span>
    )
  }

  return (
    <Tooltip>
      <TooltipTrigger>
        <button
          onClick={() => setEditing(true)}
          className="group/dur inline-flex items-center gap-0.5 rounded px-0.5 py-0.5 text-[11px] text-muted-foreground transition-colors hover:bg-accent/50 hover:text-foreground"
        >
          <span className="tabular-nums font-medium">{value}s</span>
        </button>
      </TooltipTrigger>
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  )
}

export function BlindCard({ device }: { device: BlindDevice }) {
  const [position, setPosition] = useState(device.position)
  const updateDeviceStatus = useStore((s) => s.updateDeviceStatus)
  const isDisabled = device.status === 'disabled'
  const isDiscovered = device.status === 'discovered'
  const isInteractive = device.status === 'configured'

  return (
    <Card
      className={cn(
        'group relative gap-0 overflow-hidden p-0 transition-all',
        isDisabled && 'opacity-50 grayscale',
        isDiscovered && 'border-dashed border-primary/40',
        isInteractive && 'hover:shadow-md'
      )}
    >
      {/* Discovered indicator banner */}
      {isDiscovered && (
        <div className="flex items-center justify-between bg-primary/5 px-5 py-2.5">
          <div className="flex items-center gap-2">
            <span className="relative flex size-2">
              <span className="absolute inline-flex size-full animate-ping rounded-full bg-primary opacity-75" />
              <span className="relative inline-flex size-2 rounded-full bg-primary" />
            </span>
            <span className="text-xs font-medium text-primary">New device discovered</span>
          </div>
          <Button
            size="sm"
            className="h-7 gap-1.5 text-xs"
            onClick={() => updateDeviceStatus(device.address, 'configured')}
          >
            <Plus className="size-3" />
            Add device
          </Button>
        </div>
      )}

      {/* Header zone */}
      <div className="flex items-start justify-between p-5 pb-0">
        <div className="flex flex-col gap-1.5 min-w-0 flex-1">
          <div className="flex items-center gap-2.5">
            <EditableName value={device.name} address={device.address} />
            <Badge
              variant="secondary"
              className="shrink-0 font-mono text-[10px] tracking-wider text-muted-foreground"
            >
              {device.address}
            </Badge>
          </div>
          <div className="flex items-center gap-3 text-xs text-muted-foreground">
            <span className="flex items-center gap-1.5 font-medium">
              <span className="text-card-foreground">CH {device.channel}</span>
            </span>
            <span className="flex items-center gap-1.5">
              <SignalIndicator rssi={device.rssi} />
              <span>{device.rssi} dBm</span>
            </span>
            <span className="flex items-center gap-1">
              <Timer className="size-3" />
              <ChevronUp className="size-2.5" />
              <EditableDuration
                label="Duration up (seconds)"
                value={device.durationUp}
                address={device.address}
                field="durationUp"
              />
              <ChevronDown className="size-2.5 ml-1" />
              <EditableDuration
                label="Duration down (seconds)"
                value={device.durationDown}
                address={device.address}
                field="durationDown"
              />
            </span>
          </div>
        </div>

        {!isDiscovered && (
          <div className="flex items-center gap-1 shrink-0">
            <Tooltip>
              <TooltipTrigger>
                <Button
                  variant="ghost"
                  size="icon"
                  className="size-8 text-muted-foreground hover:text-card-foreground"
                >
                  <Settings className="size-4" />
                  <span className="sr-only">Settings</span>
                </Button>
              </TooltipTrigger>
              <TooltipContent>Settings</TooltipContent>
            </Tooltip>
            <Tooltip>
              <TooltipTrigger>
                <div>
                  <Switch
                    checked={device.status === 'configured'}
                    onCheckedChange={(checked) =>
                      updateDeviceStatus(device.address, checked ? 'configured' : 'disabled')
                    }
                    className="scale-75"
                    aria-label={`${isDisabled ? 'Enable' : 'Disable'} ${device.name}`}
                  />
                </div>
              </TooltipTrigger>
              <TooltipContent>{isDisabled ? 'Enable' : 'Disable'} device</TooltipContent>
            </Tooltip>
          </div>
        )}
      </div>

      {/* Position display + slider */}
      <div className={cn('px-5 pt-5 pb-4', !isInteractive && 'pointer-events-none')}>
        <div className="flex items-baseline gap-1 pb-3">
          <span className="text-3xl font-bold tabular-nums tracking-tight text-card-foreground">
            {position}
          </span>
          <span className="text-sm font-medium text-muted-foreground">%</span>
        </div>
        <Slider
          value={[position]}
          onValueChange={(v) => setPosition(v[0])}
          max={100}
          step={1}
          className="w-full"
          disabled={!isInteractive}
        />
      </div>

      {/* Separator */}
      <div className="mx-5 border-t border-border" />

      {/* Action buttons */}
      <div
        className={cn(
          'grid grid-cols-4 gap-2 p-4 px-5',
          !isInteractive && 'pointer-events-none'
        )}
      >
        <Button variant="outline" size="sm" className="gap-1.5 text-xs font-medium" disabled={!isInteractive}>
          <ChevronUp className="size-3.5" />
          Open
        </Button>
        <Button variant="outline" size="sm" className="gap-1.5 text-xs font-medium" disabled={!isInteractive}>
          <Square className="size-3" />
          Stop
        </Button>
        <Button variant="outline" size="sm" className="gap-1.5 text-xs font-medium" disabled={!isInteractive}>
          <ChevronDown className="size-3.5" />
          Close
        </Button>
        <Button variant="outline" size="sm" className="gap-1.5 text-xs font-medium" disabled={!isInteractive}>
          <Shrink className="size-3.5" />
          Tilt
        </Button>
      </div>
    </Card>
  )
}
