import { Card } from './ui/card'
import { Input } from './ui/input'
import { Label } from './ui/label'
import { Button } from './ui/button'
import { Badge } from './ui/badge'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { Radio, Send, RotateCcw, Info, CheckCircle2, AlertCircle, Loader2 } from './icons'
import { cn } from '@/lib/utils'
import { useStore, type CC1101Registers, type DebugPayload, type SendStatus } from '@/store'

function HexField({ id, label, value, onChange, register, tooltip, className }: {
  id: string; label: string; value: string; onChange: (v: string) => void
  register?: string; tooltip?: string; className?: string
}) {
  return (
    <div className={cn('flex flex-col gap-1.5', className)}>
      <div className="flex items-center gap-2">
        <Label htmlFor={id} className="text-xs font-medium text-foreground">{label}</Label>
        {register && <Badge variant="secondary" className="px-1.5 py-0 font-mono text-[10px] tracking-wider text-muted-foreground">{register}</Badge>}
        {tooltip && (
          <Tooltip>
            <TooltipTrigger><Info className="size-3 text-muted-foreground" /></TooltipTrigger>
            <TooltipContent className="max-w-48 text-xs">{tooltip}</TooltipContent>
          </Tooltip>
        )}
      </div>
      <Input id={id} value={value} onInput={(e) => onChange((e.target as HTMLInputElement).value)} placeholder="0x00" className="h-8 font-mono text-sm" />
    </div>
  )
}

function StatusFeedback({ status, timestamp }: { status: SendStatus; timestamp: string | null }) {
  if (status === 'idle') return null
  return (
    <div className={cn(
      'flex items-center gap-2 rounded-lg px-3 py-2 text-xs font-medium transition-all',
      status === 'sending' && 'bg-muted text-muted-foreground',
      status === 'success' && 'bg-success/10 text-success',
      status === 'error' && 'bg-destructive/10 text-destructive'
    )}>
      {status === 'sending' && <><Loader2 className="size-3.5 animate-spin" />Sending...</>}
      {status === 'success' && <><CheckCircle2 className="size-3.5" />Sent{timestamp && <span className="ml-auto font-mono text-[11px] text-muted-foreground">{timestamp}</span>}</>}
      {status === 'error' && <><AlertCircle className="size-3.5" />Failed</>}
    </div>
  )
}

export function HubPanel() {
  const registers = useStore((s) => s.registers)
  const payload = useStore((s) => s.payload)
  const sendStatus = useStore((s) => s.sendStatus)
  const sentTimestamp = useStore((s) => s.sentTimestamp)
  const updateRegister = useStore((s) => s.updateRegister)
  const resetRegisters = useStore((s) => s.resetRegisters)
  const updatePayload = useStore((s) => s.updatePayload)
  const resetPayload = useStore((s) => s.resetPayload)

  // Compute frequency inline
  const f2 = parseInt(registers.freq2, 16) || 0
  const f1 = parseInt(registers.freq1, 16) || 0
  const f0 = parseInt(registers.freq0, 16) || 0
  const calculatedFreq = `${(((f2 * 256 * 256 + f1 * 256 + f0) * 26000000) / (1 << 16) / 1000000).toFixed(3)} MHz`

  const handleSendPayload = () => {
    useStore.getState().setSendStatus('sending')
    setTimeout(() => {
      useStore.getState().setSendStatus('success')
      useStore.getState().setSentTimestamp(new Date().toLocaleTimeString())
      setTimeout(() => useStore.getState().setSendStatus('idle'), 4000)
    }, 800)
  }

  return (
    <div className="flex flex-col gap-6">
      <Card className="gap-0 overflow-hidden p-0">
        <div className="flex items-center justify-between border-b border-border px-5 py-4">
          <div className="flex items-center gap-3">
            <div className="flex size-8 items-center justify-center rounded-lg bg-primary/10 text-primary"><Radio className="size-4" /></div>
            <div>
              <h2 className="text-sm font-semibold text-card-foreground">CC1101 Frequency</h2>
              <p className="text-xs text-muted-foreground">Set frequency registers</p>
            </div>
          </div>
          <Button variant="outline" size="sm" className="gap-1.5 text-xs" onClick={resetRegisters}><RotateCcw className="size-3" />Reset</Button>
        </div>
        <div className="grid grid-cols-1 gap-4 p-5 sm:grid-cols-3">
          <HexField id="freq2" label="FREQ2" register="0x21" value={registers.freq2} onChange={(v) => updateRegister('freq2', v)} />
          <HexField id="freq1" label="FREQ1" register="0x7A" value={registers.freq1} onChange={(v) => updateRegister('freq1', v)} />
          <HexField id="freq0" label="FREQ0" register="0x71" value={registers.freq0} onChange={(v) => updateRegister('freq0', v)} />
        </div>
        <div className="flex items-center gap-3 border-t border-border bg-muted/30 px-5 py-3">
          <span className="text-xs text-muted-foreground">Calculated:</span>
          <Badge variant="secondary" className="font-mono text-xs">{calculatedFreq}</Badge>
        </div>
      </Card>

      <Card className="gap-0 overflow-hidden p-0">
        <div className="flex items-center gap-3 border-b border-border px-5 py-4">
          <div className="flex size-8 items-center justify-center rounded-lg bg-warning/10 text-warning-foreground"><Send className="size-4" /></div>
          <div>
            <h2 className="text-sm font-semibold text-card-foreground">Debug Payload</h2>
            <p className="text-xs text-muted-foreground">Send raw command</p>
          </div>
        </div>
        <div className="flex flex-col gap-0 divide-y divide-border">
          <div className="flex flex-col gap-3 p-5">
            <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Addressing</span>
            <div className="grid grid-cols-1 gap-4 sm:grid-cols-3">
              <HexField id="blind-address" label="blind_address" value={payload.blindAddress} onChange={(v) => updatePayload('blindAddress', v)} />
              <HexField id="channel" label="channel" value={payload.channel} onChange={(v) => updatePayload('channel', v)} />
              <HexField id="remote-address" label="remote_address" value={payload.remoteAddress} onChange={(v) => updatePayload('remoteAddress', v)} />
            </div>
          </div>
          <div className="flex flex-col gap-3 p-5">
            <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Packet</span>
            <div className="grid grid-cols-2 gap-4 sm:grid-cols-5">
              <HexField id="payload-1" label="payload_1" value={payload.payload1} onChange={(v) => updatePayload('payload1', v)} />
              <HexField id="payload-2" label="payload_2" value={payload.payload2} onChange={(v) => updatePayload('payload2', v)} />
              <HexField id="pck-inf1" label="pck_inf1" value={payload.pckInf1} onChange={(v) => updatePayload('pckInf1', v)} />
              <HexField id="pck-inf2" label="pck_inf2" value={payload.pckInf2} onChange={(v) => updatePayload('pckInf2', v)} />
              <HexField id="hop" label="hop" value={payload.hop} onChange={(v) => updatePayload('hop', v)} />
            </div>
          </div>
          <div className="flex flex-col gap-3 p-5">
            <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Commands</span>
            <div className="grid grid-cols-2 gap-4 sm:grid-cols-5">
              <HexField id="command-check" label="cmd_check" value={payload.commandCheck} onChange={(v) => updatePayload('commandCheck', v)} />
              <HexField id="command-stop" label="cmd_stop" value={payload.commandStop} onChange={(v) => updatePayload('commandStop', v)} />
              <HexField id="command-up" label="cmd_up" value={payload.commandUp} onChange={(v) => updatePayload('commandUp', v)} />
              <HexField id="command-down" label="cmd_down" value={payload.commandDown} onChange={(v) => updatePayload('commandDown', v)} />
              <HexField id="command-tilt" label="cmd_tilt" value={payload.commandTilt} onChange={(v) => updatePayload('commandTilt', v)} />
            </div>
          </div>
        </div>
        <div className="flex items-center justify-between gap-3 border-t border-border bg-muted/30 px-5 py-3">
          <StatusFeedback status={sendStatus} timestamp={sentTimestamp} />
          <div className="ml-auto flex items-center gap-2">
            <Button variant="outline" size="sm" className="gap-1.5 text-xs" onClick={resetPayload}><RotateCcw className="size-3" />Reset</Button>
            <Button size="sm" className="gap-1.5 text-xs" onClick={handleSendPayload} disabled={sendStatus === 'sending'}>
              {sendStatus === 'sending' ? <Loader2 className="size-3 animate-spin" /> : <Send className="size-3" />}Send
            </Button>
          </div>
        </div>
      </Card>
    </div>
  )
}
