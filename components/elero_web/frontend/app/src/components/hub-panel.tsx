import { useSignal } from '@preact/signals'
import { Card } from './ui/card'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Radio, Send } from './icons'
import { useStore, parseFreq } from '@/store'
import { sendRawCommand } from '@/ws'

// Command presets for quick selection
const COMMAND_PRESETS = [
  { label: 'CHECK', value: '0x00' },
  { label: 'STOP', value: '0x10' },
  { label: 'UP', value: '0x20' },
  { label: 'TILT', value: '0x24' },
  { label: 'DOWN', value: '0x40' },
  { label: 'INT', value: '0x44' },
] as const

export function HubPanel() {
  const freq = useStore((s) => s.config.freq)
  const device = useStore((s) => s.config.device)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)

  // Raw TX form state
  const dstAddr = useSignal('0x')
  const srcAddr = useSignal('0x')
  const channel = useSignal(1)
  const cmd = useSignal('0x00')
  const showAdvanced = useSignal(false)
  const payload1 = useSignal('0x00')
  const payload2 = useSignal('0x04')
  const msgType = useSignal('0x6a')
  const type2 = useSignal('0x00')
  const hop = useSignal('0x0a')

  // Compute frequency from registers
  const f2 = parseFreq(freq.freq2, 0x21)
  const f1 = parseFreq(freq.freq1, 0x71)
  const f0 = parseFreq(freq.freq0, 0x7a)
  const calculatedFreq = `${(((f2 * 256 * 256 + f1 * 256 + f0) * 26000000) / (1 << 16) / 1000000).toFixed(3)} MHz`

  const handleSendRaw = () => {
    sendRawCommand({
      dst_address: dstAddr.value,
      src_address: srcAddr.value,
      channel: channel.value,
      command: cmd.value,
      payload_1: payload1.value,
      payload_2: payload2.value,
      msg_type: msgType.value,
      type2: type2.value,
      hop: hop.value,
    })
  }

  // Check if form is valid
  const isValidHex = (s: string) => /^0x[0-9a-fA-F]+$/.test(s)
  const isFormValid = isValidHex(dstAddr.value) && isValidHex(srcAddr.value) && isValidHex(cmd.value) && channel.value >= 0 && channel.value <= 255

  return (
    <div className="flex flex-col gap-6">
      {/* Device Info */}
      <Card className="gap-0 overflow-hidden p-0">
        <div className="flex items-center gap-3 border-b border-border px-5 py-4">
          <div className="flex size-8 items-center justify-center rounded-lg bg-primary/10 text-primary">
            <Radio className="size-4" />
          </div>
          <div>
            <h2 className="text-sm font-semibold text-card-foreground">Hub Information</h2>
            <p className="text-xs text-muted-foreground">Device configuration</p>
          </div>
        </div>
        <div className="grid grid-cols-2 gap-4 p-5 sm:grid-cols-4">
          <div className="flex flex-col gap-1">
            <span className="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
              Device
            </span>
            <span className="text-sm font-medium">{device || '-'}</span>
          </div>
          <div className="flex flex-col gap-1">
            <span className="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
              Blinds
            </span>
            <span className="text-sm font-medium">{blinds.length}</span>
          </div>
          <div className="flex flex-col gap-1">
            <span className="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
              Lights
            </span>
            <span className="text-sm font-medium">{lights.length}</span>
          </div>
          <div className="flex flex-col gap-1">
            <span className="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
              Frequency
            </span>
            <span className="text-sm font-medium">{calculatedFreq}</span>
          </div>
        </div>
      </Card>

      {/* Frequency Registers */}
      <Card className="gap-0 overflow-hidden p-0">
        <div className="flex items-center justify-between border-b border-border px-5 py-4">
          <div>
            <h2 className="text-sm font-semibold text-card-foreground">CC1101 Registers</h2>
            <p className="text-xs text-muted-foreground">Frequency configuration</p>
          </div>
        </div>
        <div className="grid grid-cols-3 gap-4 p-5">
          <div className="flex flex-col gap-1.5">
            <div className="flex items-center gap-2">
              <span className="text-xs font-medium text-foreground">FREQ2</span>
              <Badge variant="secondary" className="px-1.5 py-0 font-mono text-[10px]">0x0D</Badge>
            </div>
            <code className="rounded bg-muted px-2 py-1.5 font-mono text-sm">
              0x{f2.toString(16).padStart(2, '0')}
            </code>
          </div>
          <div className="flex flex-col gap-1.5">
            <div className="flex items-center gap-2">
              <span className="text-xs font-medium text-foreground">FREQ1</span>
              <Badge variant="secondary" className="px-1.5 py-0 font-mono text-[10px]">0x0E</Badge>
            </div>
            <code className="rounded bg-muted px-2 py-1.5 font-mono text-sm">
              0x{f1.toString(16).padStart(2, '0')}
            </code>
          </div>
          <div className="flex flex-col gap-1.5">
            <div className="flex items-center gap-2">
              <span className="text-xs font-medium text-foreground">FREQ0</span>
              <Badge variant="secondary" className="px-1.5 py-0 font-mono text-[10px]">0x0F</Badge>
            </div>
            <code className="rounded bg-muted px-2 py-1.5 font-mono text-sm">
              0x{f0.toString(16).padStart(2, '0')}
            </code>
          </div>
        </div>
        <div className="flex items-center gap-3 border-t border-border bg-muted/30 px-5 py-3">
          <span className="text-xs text-muted-foreground">Calculated:</span>
          <Badge variant="secondary" className="font-mono text-xs">{calculatedFreq}</Badge>
        </div>
      </Card>

      {/* Raw TX Form */}
      <Card className="gap-0 overflow-hidden p-0">
        <div className="flex items-center gap-3 border-b border-border px-5 py-4">
          <div className="flex size-8 items-center justify-center rounded-lg bg-primary/10 text-primary">
            <Send className="size-4" />
          </div>
          <div>
            <h2 className="text-sm font-semibold text-card-foreground">Raw TX</h2>
            <p className="text-xs text-muted-foreground">Send raw RF command for testing</p>
          </div>
        </div>

        <div className="space-y-4 p-5">
          {/* Main fields */}
          <div className="grid grid-cols-2 gap-4 sm:grid-cols-4">
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">dst_address</label>
              <input
                type="text"
                value={dstAddr.value}
                onInput={(e) => { dstAddr.value = (e.target as HTMLInputElement).value }}
                placeholder="0x313238"
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              />
            </div>
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">src_address</label>
              <input
                type="text"
                value={srcAddr.value}
                onInput={(e) => { srcAddr.value = (e.target as HTMLInputElement).value }}
                placeholder="0x17a753"
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              />
            </div>
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">Channel</label>
              <input
                type="number"
                value={channel.value}
                onInput={(e) => { channel.value = parseInt((e.target as HTMLInputElement).value) || 0 }}
                min="0"
                max="255"
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              />
            </div>
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">Command</label>
              <select
                value={cmd.value}
                onChange={(e) => { cmd.value = (e.target as HTMLSelectElement).value }}
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              >
                {COMMAND_PRESETS.map((preset) => (
                  <option key={preset.value} value={preset.value}>
                    {preset.label} ({preset.value})
                  </option>
                ))}
              </select>
            </div>
          </div>

          {/* Advanced fields toggle */}
          <button
            type="button"
            onClick={() => { showAdvanced.value = !showAdvanced.value }}
            className="text-xs text-muted-foreground hover:text-foreground"
          >
            {showAdvanced.value ? '- Hide advanced fields' : '+ Show advanced fields'}
          </button>

          {/* Advanced fields */}
          {showAdvanced.value && (
            <div className="grid grid-cols-2 gap-4 sm:grid-cols-5">
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">payload_1</label>
                <input
                  type="text"
                  value={payload1.value}
                  onInput={(e) => { payload1.value = (e.target as HTMLInputElement).value }}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">payload_2</label>
                <input
                  type="text"
                  value={payload2.value}
                  onInput={(e) => { payload2.value = (e.target as HTMLInputElement).value }}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">type</label>
                <input
                  type="text"
                  value={msgType.value}
                  onInput={(e) => { msgType.value = (e.target as HTMLInputElement).value }}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">type2</label>
                <input
                  type="text"
                  value={type2.value}
                  onInput={(e) => { type2.value = (e.target as HTMLInputElement).value }}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">hop</label>
                <input
                  type="text"
                  value={hop.value}
                  onInput={(e) => { hop.value = (e.target as HTMLInputElement).value }}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
            </div>
          )}
        </div>

        {/* Send button */}
        <div className="flex items-center justify-end gap-3 border-t border-border bg-muted/30 px-5 py-3">
          <Button
            onClick={handleSendRaw}
            disabled={!isFormValid}
            className="gap-2"
          >
            <Send className="size-4" />
            Send Raw TX
          </Button>
        </div>
      </Card>
    </div>
  )
}
