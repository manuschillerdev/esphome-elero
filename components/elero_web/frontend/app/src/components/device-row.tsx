import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { InlineEdit } from './ui/inline-edit'
import { SignalIndicator } from './signal-indicator'
import { formatTime } from './packet-table'
import { ChevronUp, Square, ChevronDown, Shrink, Lightbulb, LightbulbOff, Settings, RotateCcw, Save, Info, CheckCircle2, Trash2 } from './icons'
import { cn } from '@/lib/utils'
import {
  updateDevice, getStateLabel, getCommandLabel, isMovingState, isCommandPacket, isButtonPacket,
  rfPackets, hub, displayNames,
  type Device, type RfPacketWithTimestamp,
} from '@/store'
import { sendDeviceCommand, sendRawCommand, sendUpsertDevice, sendRemoveDevice } from '@/ws'

// ─── Shared cell renderers (used by DataTable column definitions) ───────────

export function StatusDot({ device }: { device: Device }) {
  const unsaved = device.updated_at === null
  const label = unsaved ? 'Unsaved' : device.enabled ? 'Saved & published' : 'Saved & unpublished'
  const dot = unsaved ? (
    <span className="relative flex size-2 shrink-0">
      <span className="absolute inline-flex size-full animate-ping rounded-full bg-orange-400 opacity-75" />
      <span className="relative inline-flex size-2 rounded-full bg-orange-400" />
    </span>
  ) : (
    <span className={cn('inline-flex size-2 shrink-0 rounded-full', device.enabled ? 'bg-emerald-500' : 'bg-muted-foreground/40')} />
  )
  return (
    <Tooltip>
      <TooltipTrigger>{dot}</TooltipTrigger>
      <TooltipContent>{label}</TooltipContent>
    </Tooltip>
  )
}

export function DeviceCell({ device }: { device: Device }) {
  return (
    <div className="flex min-w-0 items-center gap-2">
      <StatusDot device={device} />
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium text-foreground">
          <InlineEdit
            value={device.name || `Unnamed cover (${device.address})`}
            onSave={(name) => updateDevice(device.address, { name })}
          />
        </span>
        <div className="flex items-center gap-2 text-[10px] text-muted-foreground">
          <span className="font-mono">{device.address}</span>
          <span>CH {device.channel}</span>
        </div>
      </div>
    </div>
  )
}

export function LightDeviceCell({ device }: { device: Device }) {
  return (
    <div className="flex min-w-0 items-center gap-2">
      <StatusDot device={device} />
      <div className="flex min-w-0 flex-col gap-0.5">
        <span className="truncate text-sm font-medium text-foreground">
          <InlineEdit
            value={device.name || `Unnamed light (${device.address})`}
            onSave={(name) => updateDevice(device.address, { name })}
          />
        </span>
        <div className="flex items-center gap-2 text-[10px] text-muted-foreground">
          <span className="font-mono">{device.address}</span>
          <span>CH {device.channel}</span>
        </div>
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


export function BlindControls({ device }: { device: Device }) {
  return (
    <div className="flex items-center gap-1 text-primary">
      <Tooltip>
        <TooltipTrigger>
          <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary disabled:text-muted-foreground/40 disabled:pointer-events-none" disabled={!device.supports_tilt} onClick={() => sendDeviceCommand(device, 'tilt')}>
            <Shrink className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent>{device.supports_tilt ? 'Tilt' : 'Tilt (disabled)'}</TooltipContent>
      </Tooltip>
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

export function LightControls({ device }: { device: Device }) {
  return (
    <div className="flex items-center gap-1">
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

export function DeviceActions({ device, expanded, onToggleExpand }: {
  device: Device
  expanded?: boolean
  onToggleExpand?: () => void
}) {
  return (
    <div className="flex items-center gap-1">
      {hub.value.crud && (
        <Tooltip>
          <TooltipTrigger>
            <Button variant="ghost" size="icon" className="size-7 text-primary hover:text-primary"
              onClick={() => sendUpsertDevice(device)}>
              {device.updated_at !== null
                ? <CheckCircle2 className="size-3.5" />
                : <Save className="size-3.5" />}
            </Button>
          </TooltipTrigger>
          <TooltipContent>{device.updated_at !== null ? 'Saved — click to update' : 'Save to NVS'}</TooltipContent>
        </Tooltip>
      )}
      <Tooltip>
        <TooltipTrigger>
          <Button
            variant="ghost"
            size="icon"
            className={cn('size-7 text-primary hover:text-primary', expanded && 'bg-muted')}
            onClick={onToggleExpand}
          >
            <Settings className="size-3.5" />
          </Button>
        </TooltipTrigger>
        <TooltipContent align="end">Settings</TooltipContent>
      </Tooltip>
    </div>
  )
}

// ─── Expanded Settings Panel ────────────────────────────────────────────────

function replayPacket(pkt: RfPacketWithTimestamp, device: Device) {
  sendRawCommand({
    dst_address: device.address,
    src_address: device.remote,
    channel: device.channel,
    command: pkt.command ?? '0x00',
  })
}

/** Deduplicate consecutive packets with the same command (Elero sends each command twice) */
function deduplicatePackets(pkts: RfPacketWithTimestamp[]): RfPacketWithTimestamp[] {
  const result: RfPacketWithTimestamp[] = []
  for (const pkt of pkts) {
    const prev = result[result.length - 1]
    if (prev && prev.command === pkt.command && prev.src === pkt.src && prev.type === pkt.type) continue
    result.push(pkt)
  }
  return result
}

const inputClass = 'h-7 w-20 rounded-md border border-input bg-background px-2 text-xs tabular-nums outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]'

function Toggle({ checked, onChange, label }: { checked: boolean; onChange: (v: boolean) => void; label: string }) {
  return (
    <label className="flex items-center gap-2 text-xs cursor-pointer">
      <button
        type="button"
        role="switch"
        aria-checked={checked}
        onClick={() => onChange(!checked)}
        className={cn(
          'relative inline-flex h-4 w-7 shrink-0 rounded-full border-2 border-transparent transition-colors focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring',
          checked ? 'bg-primary' : 'bg-input',
        )}
      >
        <span className={cn(
          'pointer-events-none block size-3 rounded-full bg-background shadow-sm transition-transform',
          checked ? 'translate-x-3' : 'translate-x-0',
        )} />
      </button>
      <span className="text-muted-foreground">{label}</span>
    </label>
  )
}

export function DeviceExpandedPanel({ device }: { device: Device }) {
  const crudEnabled = hub.value.crud
  const packets = rfPackets.value

  const commandPackets = deduplicatePackets(
    packets.filter((pkt) =>
      (isCommandPacket(pkt) || isButtonPacket(pkt)) &&
      !pkt.echo &&
      pkt.src === device.remote &&
      (pkt.dst === device.address || pkt.channel === device.channel)
    )
  ).slice(-10).reverse()

  return (
    <div className="border-t border-border bg-muted/20">
      {/* Config row */}
      <div className="flex items-center justify-between px-5 py-3 border-b border-border">
        {/* Left: config inputs */}
        <div className="flex items-center gap-4">
          {device.type === 'cover' && (
            <>
              <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
                <span>&#x25B3;</span>
                <input
                  type="number"
                  value={+(device.open_ms / 1000).toFixed(1)}
                  onInput={(e) => {
                    const v = parseFloat((e.target as HTMLInputElement).value)
                    if (!isNaN(v)) updateDevice(device.address, { open_ms: Math.round(v * 1000) })
                  }}
                  min={0} max={300} step={0.1}
                  className={inputClass}
                />
                <span>s</span>
              </label>
              <label className="flex items-center gap-1.5 text-xs text-muted-foreground">
                <span>&#x25BD;</span>
                <input
                  type="number"
                  value={+(device.close_ms / 1000).toFixed(1)}
                  onInput={(e) => {
                    const v = parseFloat((e.target as HTMLInputElement).value)
                    if (!isNaN(v)) updateDevice(device.address, { close_ms: Math.round(v * 1000) })
                  }}
                  min={0} max={300} step={0.1}
                  className={inputClass}
                />
                <span>s</span>
              </label>
              <Toggle checked={device.supports_tilt} onChange={(v) => updateDevice(device.address, { supports_tilt: v })} label="Tilt" />
            </>
          )}
        </div>

        {/* Right: published toggle + save/delete */}
        <div className="flex items-center gap-2">
          <div className="flex items-center gap-1.5">
            <Toggle checked={device.enabled} onChange={(v) => updateDevice(device.address, { enabled: v })} label="Published" />
            <Tooltip>
              <TooltipTrigger>
                <Info className="size-3 text-muted-foreground/60" />
              </TooltipTrigger>
              <TooltipContent>Controls whether this device is published to Home Assistant</TooltipContent>
            </Tooltip>
          </div>
          {crudEnabled && device.updated_at !== null && (
            <>
              <div className="mx-0.5 h-4 w-px bg-border" />
              <Tooltip>
                <TooltipTrigger>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-7 text-destructive hover:text-destructive"
                    onClick={() => sendRemoveDevice(device.address, device.type === 'light' ? 'light' : 'cover')}
                  >
                    <Trash2 className="size-3.5" />
                  </Button>
                </TooltipTrigger>
                <TooltipContent align="end">Remove from NVS</TooltipContent>
              </Tooltip>
            </>
          )}
        </div>
      </div>

      {/* Sniffed commands */}
      <div className="px-5 py-3">
        <span className="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
          Sniffed commands from {displayNames.value[device.remote] ?? device.remote}
        </span>
        {commandPackets.length === 0 ? (
          <p className="mt-2 text-xs text-muted-foreground">
            No command packets captured yet — press buttons on the physical remote.
          </p>
        ) : (
          <div className="mt-2 max-h-[180px] overflow-y-auto">
            <table className="w-full text-xs font-mono">
              <thead className="sticky top-0 bg-muted/20">
                <tr className="text-left text-[10px] text-muted-foreground">
                  <th className="pb-1 pr-3 font-medium">Time</th>
                  <th className="pb-1 pr-3 font-medium">Command</th>
                  <th className="pb-1 pr-3 font-medium">Type</th>
                  <th className="pb-1 pr-3 font-medium">RSSI</th>
                  <th className="pb-1 font-medium text-right">Replay</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-border/50">
                {commandPackets.map((pkt, i) => (
                  <tr key={`${pkt.t}-${i}`} className="group">
                    <td className="py-1.5 pr-3 text-muted-foreground">{formatTime(pkt.received_at)}</td>
                    <td className="py-1.5 pr-3">{getCommandLabel(pkt.command)}</td>
                    <td className="py-1.5 pr-3 text-muted-foreground">{pkt.type}</td>
                    <td className="py-1.5 pr-3 text-muted-foreground">{pkt.rssi?.toFixed(0) ?? '-'}</td>
                    <td className="py-1.5 text-right">
                      <Tooltip>
                        <TooltipTrigger>
                          <Button
                            variant="ghost"
                            size="icon"
                            className="size-6 text-muted-foreground hover:text-primary"
                            onClick={() => replayPacket(pkt, device)}
                          >
                            <RotateCcw className="size-3" />
                          </Button>
                        </TooltipTrigger>
                        <TooltipContent>Replay as hub</TooltipContent>
                      </Tooltip>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>
    </div>
  )
}
