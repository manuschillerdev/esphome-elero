import { useState, useRef, useEffect } from 'preact/compat'
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

function InlineEditName({ value, address }: { value: string; address: string }) {
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
        className="h-6 w-28 min-w-0 rounded border border-input bg-background px-1.5 text-sm font-semibold text-foreground outline-none focus:ring-2 focus:ring-ring"
      />
    )
  }

  return (
    <button
      onClick={() => setEditing(true)}
      className="group/name flex items-center gap-1 rounded px-1 py-0.5 -ml-1 text-left transition-colors hover:bg-accent/50"
    >
      <span className="text-sm font-semibold text-foreground">{value}</span>
      <Pencil className="size-2.5 text-muted-foreground opacity-0 transition-opacity group-hover/name:opacity-100" />
    </button>
  )
}

function InlineEditDuration({
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
          className="h-4 w-8 rounded border border-input bg-background px-0.5 text-center text-[10px] font-medium tabular-nums text-foreground outline-none focus:ring-1 focus:ring-ring"
        />
        <span className="text-[10px] text-muted-foreground">s</span>
      </span>
    )
  }

  return (
    <Tooltip>
      <TooltipTrigger>
        <button
          onClick={() => setEditing(true)}
          className="inline-flex items-center gap-0.5 rounded px-0.5 text-[10px] text-muted-foreground transition-colors hover:bg-accent/50 hover:text-foreground"
        >
          <span className="tabular-nums font-medium">{value}s</span>
        </button>
      </TooltipTrigger>
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  )
}

export function BlindListRow({ device }: { device: BlindDevice }) {
  const [position, setPosition] = useState(device.position)
  const updateDeviceStatus = useStore((s) => s.updateDeviceStatus)
  const isDisabled = device.status === 'disabled'
  const isDiscovered = device.status === 'discovered'
  const isInteractive = device.status === 'configured'

  return (
    <div
      className={cn(
        'group flex items-center gap-4 rounded-xl border bg-card px-4 py-3 transition-all',
        isDisabled && 'opacity-50 grayscale',
        isDiscovered && 'border-dashed border-primary/40',
        isInteractive && 'hover:shadow-sm'
      )}
    >
      {/* Name + meta column */}
      <div className="flex min-w-0 flex-col gap-0.5" style={{ width: '220px', flexShrink: 0 }}>
        <div className="flex items-center gap-2">
          <InlineEditName value={device.name} address={device.address} />
          {isDiscovered && (
            <span className="relative flex size-2 shrink-0">
              <span className="absolute inline-flex size-full animate-ping rounded-full bg-primary opacity-75" />
              <span className="relative inline-flex size-2 rounded-full bg-primary" />
            </span>
          )}
        </div>
        <div className="flex items-center gap-2 text-[11px] text-muted-foreground">
          <Badge
            variant="secondary"
            className="font-mono text-[9px] tracking-wider text-muted-foreground px-1 py-0"
          >
            {device.address}
          </Badge>
          <span>CH {device.channel}</span>
          <span className="flex items-center gap-1">
            <SignalIndicator rssi={device.rssi} />
            {device.rssi} dBm
          </span>
        </div>
        <div className="flex items-center gap-1 text-[10px] text-muted-foreground mt-0.5">
          <Timer className="size-2.5" />
          <ChevronUp className="size-2" />
          <InlineEditDuration
            label="Duration up (seconds)"
            value={device.durationUp}
            address={device.address}
            field="durationUp"
          />
          <ChevronDown className="size-2 ml-0.5" />
          <InlineEditDuration
            label="Duration down (seconds)"
            value={device.durationDown}
            address={device.address}
            field="durationDown"
          />
        </div>
      </div>

      {/* Position + slider */}
      <div
        className={cn('flex flex-1 items-center gap-3', !isInteractive && 'pointer-events-none')}
      >
        <span className="w-10 text-right text-sm font-bold tabular-nums text-card-foreground">
          {position}%
        </span>
        <Slider
          value={[position]}
          onValueChange={(v) => setPosition(v[0])}
          max={100}
          step={1}
          className="flex-1"
          disabled={!isInteractive}
        />
      </div>

      {/* Actions */}
      <div
        className={cn('flex items-center gap-1 shrink-0', !isInteractive && 'pointer-events-none')}
      >
        <Tooltip>
          <TooltipTrigger>
            <Button variant="outline" size="icon" className="size-8" disabled={!isInteractive}>
              <ChevronUp className="size-3.5" />
              <span className="sr-only">Open</span>
            </Button>
          </TooltipTrigger>
          <TooltipContent>Open</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="outline" size="icon" className="size-8" disabled={!isInteractive}>
              <Square className="size-3" />
              <span className="sr-only">Stop</span>
            </Button>
          </TooltipTrigger>
          <TooltipContent>Stop</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="outline" size="icon" className="size-8" disabled={!isInteractive}>
              <ChevronDown className="size-3.5" />
              <span className="sr-only">Close</span>
            </Button>
          </TooltipTrigger>
          <TooltipContent>Close</TooltipContent>
        </Tooltip>
        <Tooltip>
          <TooltipTrigger>
            <Button variant="outline" size="icon" className="size-8" disabled={!isInteractive}>
              <Shrink className="size-3.5" />
              <span className="sr-only">Tilt</span>
            </Button>
          </TooltipTrigger>
          <TooltipContent>Tilt</TooltipContent>
        </Tooltip>
      </div>

      {/* Status / Settings */}
      <div className="flex items-center gap-1 shrink-0 border-l border-border pl-3">
        {isDiscovered ? (
          <Button
            size="sm"
            className="h-7 gap-1.5 text-xs"
            onClick={() => updateDeviceStatus(device.address, 'configured')}
          >
            <Plus className="size-3" />
            Add
          </Button>
        ) : (
          <>
            <Tooltip>
              <TooltipTrigger>
                <Button
                  variant="ghost"
                  size="icon"
                  className="size-7 text-muted-foreground hover:text-card-foreground"
                >
                  <Settings className="size-3.5" />
                  <span className="sr-only">Settings</span>
                </Button>
              </TooltipTrigger>
              <TooltipContent>Settings</TooltipContent>
            </Tooltip>
            <Switch
              checked={device.status === 'configured'}
              onCheckedChange={(checked) =>
                updateDeviceStatus(device.address, checked ? 'configured' : 'disabled')
              }
              className="scale-75"
              aria-label={`${isDisabled ? 'Enable' : 'Disable'} ${device.name}`}
            />
          </>
        )}
      </div>
    </div>
  )
}
