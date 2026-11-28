import { useComputed, useSignal } from '@preact/signals'
import { useEffect } from 'preact/hooks'
import { learnIn, devices, hub } from '@/store'
import { sendLearnInStart, sendLearnInConfirmUp, sendLearnInConfirmDown, sendLearnInCancel } from '@/ws'
import { cn } from '@/lib/utils'
import { Card } from './ui/card'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Input } from './ui/input'
import { AlertCircle, CheckCircle2, ChevronDown, ChevronUp, Loader2, Power, Radio, RemoteControl, RotateCcw, Send, Square } from './icons'

const selectChevronBg = "url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%236b7280' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E\")"
const selectClass = 'h-9 w-full appearance-none rounded-md border border-input bg-transparent bg-[length:16px_16px] bg-[right_8px_center] bg-no-repeat pl-3 pr-8 text-sm shadow-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]'
const fieldClass = 'flex flex-col gap-1.5'
const labelClass = 'text-[11px] font-medium uppercase tracking-wider text-muted-foreground'

function isValidHexAddress(value: string) {
  return /^0x[0-9a-fA-F]{6}$/.test(value)
}

const FALLBACK_SRC_ADDRESS = '0x000001'

const operations = {
  power_cycle: 'Pair after a power cycle',
  open_standby: 'Open standby with a paired gateway channel',
  train: 'Pair with the receiver in standby',
  remove_link: 'Remove a gateway channel from receivers',
} as const
type Operation = keyof typeof operations

function stateLabel(state: string) {
  return state.replace(/_/g, ' ').replace(/\b\w/g, (c: string) => c.toUpperCase())
}

function stateTone(state: string) {
  if (state === 'complete') return 'bg-success/15 text-success'
  if (state === 'failed' || state === 'timed_out' || state === 'cancelled') return 'bg-destructive/10 text-destructive'
  if (state === 'wait_up' || state === 'wait_down') return 'bg-warning/20 text-warning-foreground'
  return 'bg-primary/10 text-primary'
}

function StepRow({
  icon: Icon,
  title,
  detail,
  active = false,
  done = false,
}: {
  icon: typeof Power
  title: string
  detail: string
  active?: boolean
  done?: boolean
}) {
  return (
    <div className={cn('flex items-start gap-3 rounded-lg border px-3 py-3', active ? 'border-primary/40 bg-primary/5' : 'border-border bg-muted/20')}>
      <div className={cn('mt-0.5 flex size-8 items-center justify-center rounded-full', done ? 'bg-success/15 text-success' : active ? 'bg-primary/10 text-primary' : 'bg-muted text-muted-foreground')}>
        <Icon className={cn('size-4', active && !done && 'animate-pulse')} />
      </div>
      <div className="min-w-0 flex-1">
        <div className="flex items-center gap-2">
          <p className="text-sm font-medium text-foreground">{title}</p>
          {done && <CheckCircle2 className="size-4 text-success" />}
        </div>
        <p className="text-xs leading-relaxed text-muted-foreground">{detail}</p>
      </div>
    </div>
  )
}

export function LearnInPanel() {
  const state = learnIn.value
  const defaultSrcAddress = useComputed(() => hub.value.default_src_address || FALLBACK_SRC_ADDRESS)
  const srcAddress = useSignal(state.src_address ?? defaultSrcAddress.value)
  const appliedDefaultSrcAddress = useSignal(srcAddress.value)
  const channel = useSignal(state.channel ?? 1)
  const sessionTimeoutSec = useSignal(300)
  const selectedMode = useSignal<Operation>(state.mode ?? 'power_cycle')
  const selectedReceiver = useSignal(state.receiver_address ?? '')
  const mode = state.active ? state.mode ?? 'power_cycle' : selectedMode.value
  const legacy = mode === 'power_cycle'
  const trackedLink = mode === 'open_standby' || mode === 'remove_link'
  const savedReceivers = [...devices.value.values()].filter(d =>
    d.type !== 'remote' && d.updated_at !== null && d.remote.toLowerCase() === defaultSrcAddress.value.toLowerCase())
  const savedLinks = savedReceivers.filter(d => d.type === 'cover')
  const selectedLink = savedLinks.find(d => d.address === selectedReceiver.value)
  const linkChannel = trackedLink ? selectedLink?.channel : channel.value
  const shared = savedReceivers.filter(d => d.channel === linkChannel).map(d => d.name || d.address)
  const movement = state.state === 'watch_movement'
  const setOperation = (value: Operation) => {
    selectedMode.value = value
    if (value === 'open_standby' || value === 'remove_link') selectedReceiver.value = savedLinks[0]?.address ?? ''
  }

  const remoteOptions = [...devices.value.values()]
    .filter((device) => device.type === 'remote')
    .sort((a, b) => a.address.localeCompare(b.address))

  const formValid = (
    (legacy ? isValidHexAddress(srcAddress.value) : isValidHexAddress(selectedReceiver.value))
    && linkChannel !== undefined && Number.isInteger(linkChannel) && linkChannel >= 1 && linkChannel <= 255
    && sessionTimeoutSec.value >= 5
  )

  const isWaitingUp = movement || state.state === 'wait_up'
  const isWaitingDown = movement || state.state === 'wait_down'
  const isBusy = state.busy
  const active = state.active
  const canStart = !active && !isBusy && formValid

  useEffect(() => {
    const next_default = state.src_address ?? defaultSrcAddress.value
    if (state.src_address) {
      srcAddress.value = state.src_address
      appliedDefaultSrcAddress.value = state.src_address
      return
    }
    if (!isValidHexAddress(srcAddress.value) ||
        srcAddress.value === appliedDefaultSrcAddress.value) {
      srcAddress.value = next_default
    }
    appliedDefaultSrcAddress.value = next_default
  }, [state.src_address, defaultSrcAddress.value])

  const handleStart = () => {
    if (!linkChannel) return
    if (mode === 'remove_link' && !window.confirm(
      `Remove gateway remote ${defaultSrcAddress.value}, channel ${linkChannel}, from every receiver that learned it? Saved receivers: ${shared.join(', ')}. Other receivers may also use this link.`,
    )) return
    sendLearnInStart({
      ...(legacy ? {} : { mode, receiver_address: selectedReceiver.value }),
      ...(mode === 'remove_link' ? { confirm_all_receivers: true } : {}),
      src_address: legacy ? srcAddress.value : defaultSrcAddress.value,
      channel: linkChannel,
      session_timeout_ms: sessionTimeoutSec.value * 1000,
    })
  }

  const currentSrc = active ? state.src_address : legacy ? srcAddress.value : defaultSrcAddress.value
  const currentChannel = active ? state.channel : linkChannel ?? '—'

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex flex-col gap-3 border-b border-border px-5 py-4 sm:flex-row sm:items-start sm:justify-between">
        <div>
          <div className="flex items-center gap-2">
            <h2 className="text-sm font-semibold text-card-foreground">Learn-In</h2>
            <Badge className={cn('border-0 font-medium', stateTone(state.state))}>{stateLabel(state.state)}</Badge>
          </div>
          <p className="mt-1 text-xs text-muted-foreground">
            Use a virtual remote to pair a motor or remove a gateway channel.
          </p>
        </div>
        <div className="flex flex-wrap items-center gap-2 text-xs text-muted-foreground">
          <span className="rounded-md bg-muted px-2 py-1 font-mono">src {currentSrc}</span>
          <span className="rounded-md bg-muted px-2 py-1 font-mono">ch {currentChannel}</span>
        </div>
      </div>

      <div className="grid gap-4 px-5 py-4 lg:grid-cols-[1.1fr_0.9fr]">
        <div className="flex flex-col gap-4">
          <label className={fieldClass}>
            <span className={labelClass}>Operation</span>
            <select className={selectClass} value={mode} disabled={active}
              onChange={e => setOperation(e.currentTarget.value as Operation)}>
              {Object.entries(operations).map(([value, title]) => <option key={value} value={value}>{title}</option>)}
            </select>
          </label>
          {!legacy && <label className={fieldClass}>
            <span className={labelClass}>{trackedLink ? 'Saved gateway link' : 'Receiver address'}</span>
            {trackedLink
              ? <select className={selectClass} disabled={active} value={selectedReceiver.value}
                  onChange={e => { selectedReceiver.value = e.currentTarget.value }}>
                  <option value="">Select a cover</option>
                  {savedLinks.map(d => <option key={d.address} value={d.address}>{d.name} ({d.address}, channel {d.channel})</option>)}
                </select>
              : <Input value={active ? state.receiver_address : selectedReceiver.value} disabled={active} placeholder="0x123456"
                  onInput={e => { selectedReceiver.value = e.currentTarget.value }} />}
          </label>}
          <div className="grid gap-4 sm:grid-cols-2">
            <div className={fieldClass}>
              <label className={labelClass}>Virtual Remote Address</label>
              <div className="flex gap-2">
                <Input
                  value={legacy ? srcAddress.value : defaultSrcAddress.value}
                  onInput={(e) => { srcAddress.value = (e.target as HTMLInputElement).value }}
                  placeholder="0x17a753"
                  disabled={active || !legacy}
                  className="font-mono"
                />
                <Button variant="secondary" onClick={() => {
                  srcAddress.value = defaultSrcAddress.value
                  appliedDefaultSrcAddress.value = defaultSrcAddress.value
                }} disabled={active || !legacy} className="shrink-0 px-3 text-xs">
                  <RotateCcw className="size-3.5" />
                  Default
                </Button>
              </div>
              {legacy && remoteOptions.length > 0 && (
                <select
                  value={remoteOptions.some((remote) => remote.address === srcAddress.value) ? srcAddress.value : ''}
                  onChange={(e) => { srcAddress.value = (e.target as HTMLSelectElement).value }}
                  disabled={active || !legacy}
                  className={selectClass}
                  style={{ backgroundImage: selectChevronBg }}
                >
                  <option value="">Pick an existing remote…</option>
                  {remoteOptions.map((remote) => (
                    <option key={remote.address} value={remote.address}>
                      {remote.name ? `${remote.name} (${remote.address})` : remote.address}
                    </option>
                  ))}
                </select>
              )}
              {legacy && <p className="text-[11px] text-muted-foreground">Defaults to a deterministic hub-derived virtual remote address; you can still override it or pick an existing remote.</p>}
            </div>

            <div className={fieldClass}>
              <label className={labelClass}>Channel</label>
              <Input
                type="number"
                min={1}
                max={255}
                value={active ? state.channel : linkChannel ?? channel.value}
                onInput={(e) => { channel.value = Number((e.target as HTMLInputElement).value) || 0 }}
                disabled={active || trackedLink}
              />
              {mode === 'train' && <p className="text-[11px] text-muted-foreground">Use the gateway channel that you want to pair.</p>}
            </div>

            <div className={fieldClass}>
              <label className={labelClass}>Session Timeout (seconds)</label>
              <Input
                type="number"
                min={5}
                value={sessionTimeoutSec.value}
                onInput={(e) => { sessionTimeoutSec.value = Number((e.target as HTMLInputElement).value) || 0 }}
                disabled={active}
              />
            </div>
          </div>

          {!formValid && !active && (
            <div className="flex items-start gap-2 rounded-lg border border-destructive/20 bg-destructive/5 px-3 py-2 text-xs text-destructive">
              <AlertCircle className="mt-0.5 size-4 shrink-0" />
              <p>{trackedLink ? 'Select a saved cover that uses the gateway remote and channel.' : 'Use a 3-byte hex address, a channel from 1 to 255 and a timeout of at least 5 seconds.'}</p>
            </div>
          )}

          <div className="flex flex-wrap gap-2 border-t border-border pt-4">
            <Button onClick={handleStart} disabled={!canStart} className="gap-2">
              {isBusy && active ? <Loader2 className="size-4 animate-spin" /> : <Send className="size-4" />}
              {legacy ? 'Start learn-in' : operations[mode]}
            </Button>
            <Button onClick={sendLearnInConfirmUp} disabled={!isWaitingUp || isBusy} variant="secondary" className="gap-2">
              <ChevronUp className="size-4" />
              Confirm UP movement
            </Button>
            <Button onClick={sendLearnInConfirmDown} disabled={!isWaitingDown || isBusy} variant="secondary" className="gap-2">
              <ChevronDown className="size-4" />
              Confirm DOWN movement
            </Button>
            <Button onClick={sendLearnInCancel} disabled={!active} variant="outline" className="gap-2">
              <Square className="size-4" />
              Cancel
            </Button>
          </div>
        </div>

        <div className="flex flex-col gap-3">
          {!legacy && <div className="rounded-lg border p-3 text-sm">
            {mode === 'open_standby' && 'Use the saved channel that already controls this motor. When the motor responds, select Pair with the receiver in standby.'}
            {mode === 'train' && 'First open standby with a paired gateway channel or with a paired hand transmitter. For a LumeroTel 2, hold UP, DOWN and P together for 3 seconds. Select the new gateway channel. Start pairing. For each movement, press the matching direction until the motor reports completion.'}
            {mode === 'remove_link' && 'This removes the gateway remote and selected channel from all receivers that learned this link. A receiver can be affected even if it is absent from the list of saved receivers.'}
          </div>}
          {legacy && <><StepRow
            icon={Power}
            title="1. Power-cycle the receiver"
            detail="Turn mains power to the motor off for a few seconds, then back on. Many elero receivers stay in programming mode for about 5 minutes."
            active={active && state.state === 'programming'}
            done={['wait_up', 'confirming_up', 'wait_down', 'confirming_down', 'complete'].includes(state.state)}
          />
          <StepRow
            icon={RemoteControl}
            title="2. Send programming / P"
            detail="Start the session to transmit the virtual remote's programming / P action."
            active={active && (state.state === 'programming' || state.state === 'wait_up')}
            done={['wait_up', 'confirming_up', 'wait_down', 'confirming_down', 'complete'].includes(state.state)}
          />
          <StepRow
            icon={ChevronUp}
            title="3. Confirm upward movement"
            detail="When the motor jogs or reacts, press Confirm UP to send the next required learn-in step."
            active={isWaitingUp || state.state === 'confirming_up'}
            done={['wait_down', 'confirming_down', 'complete'].includes(state.state)}
          />
          <StepRow
            icon={ChevronDown}
            title="4. Confirm downward movement"
            detail="After the next prompt or motor response, press Confirm DOWN to finish the procedure."
            active={isWaitingDown || state.state === 'confirming_down'}
            done={state.state === 'complete'}
          /></>}

          <div className={cn('rounded-lg border px-3 py-3 text-sm', state.state === 'complete' ? 'border-success/20 bg-success/5' : state.state === 'failed' || state.state === 'timed_out' || state.state === 'cancelled' ? 'border-destructive/20 bg-destructive/5' : 'border-border bg-muted/20')}>
            <div className="flex items-center gap-2 font-medium">
              {state.state === 'complete' ? (
                <CheckCircle2 className="size-4 text-success" />
              ) : state.busy ? (
                <Loader2 className="size-4 animate-spin text-primary" />
              ) : (
                <Radio className="size-4 text-primary" />
              )}
              Session status: {stateLabel(state.state)}
            </div>
            <p className="mt-1 text-xs leading-relaxed text-muted-foreground">
              {state.state === 'ack_sent' && 'The motor sent its completion frame. The gateway sent the reply. Check control with the new channel.'}
              {state.state === 'removal_sent' && 'The gateway sent the removal command. Check the affected receivers.'}
              {state.state === 'standby_sent' && 'The gateway sent the standby command. Check the motor before you start pairing.'}
              {state.state === 'watch_movement' && 'Watch the motor. Press the matching direction when the motor moves.'}
              {legacy && state.state === 'idle' && 'Ready to start after the target receiver has been power-cycled.'}
              {state.state === 'programming' && 'Programming action is being transmitted.'}
              {state.state === 'wait_up' && 'Motor should be ready for the first confirmation step. Press Confirm UP after movement.'}
              {state.state === 'confirming_up' && 'Sending UP confirmation.'}
              {state.state === 'wait_down' && 'First confirmation accepted. Wait for the next motor response, then press Confirm DOWN.'}
              {state.state === 'confirming_down' && 'Sending DOWN confirmation.'}
              {state.state === 'complete' && 'Learn-in completed. The next UI step is device discovery / saving the resulting motor config.'}
              {state.state === 'failed' && 'Transmission failed after retries. Check RF settings and whether the receiver is still in programming mode.'}
              {state.state === 'cancelled' && 'Session cancelled.'}
              {state.state === 'timed_out' && 'The session timed out. Open programming mode and start again.'}
            </p>
            {legacy && <p className="mt-2 text-[11px] text-muted-foreground">
              Warning: if multiple receivers share the same mains line, they may all enter programming mode together.
            </p>}
          </div>
        </div>
      </div>
    </Card>
  )
}
