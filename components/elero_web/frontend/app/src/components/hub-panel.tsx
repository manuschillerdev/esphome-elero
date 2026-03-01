import { useState } from 'preact/hooks'
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
  const [blindAddr, setBlindAddr] = useState('0x')
  const [remoteAddr, setRemoteAddr] = useState('0x')
  const [channel, setChannel] = useState(1)
  const [command, setCommand] = useState('0x00')
  const [showAdvanced, setShowAdvanced] = useState(false)
  const [payload1, setPayload1] = useState('0x00')
  const [payload2, setPayload2] = useState('0x04')
  const [pckInf1, setPckInf1] = useState('0x6a')
  const [pckInf2, setPckInf2] = useState('0x00')
  const [hop, setHop] = useState('0x0a')

  // Compute frequency from registers
  const f2 = parseFreq(freq.freq2, 0x21)
  const f1 = parseFreq(freq.freq1, 0x71)
  const f0 = parseFreq(freq.freq0, 0x7a)
  const calculatedFreq = `${(((f2 * 256 * 256 + f1 * 256 + f0) * 26000000) / (1 << 16) / 1000000).toFixed(3)} MHz`

  const handleSendRaw = () => {
    sendRawCommand({
      blind_address: blindAddr,
      remote_address: remoteAddr,
      channel,
      command,
      payload_1: payload1,
      payload_2: payload2,
      pck_inf1: pckInf1,
      pck_inf2: pckInf2,
      hop,
    })
  }

  // Check if form is valid
  const isValidHex = (s: string) => /^0x[0-9a-fA-F]+$/.test(s)
  const isFormValid = isValidHex(blindAddr) && isValidHex(remoteAddr) && isValidHex(command) && channel >= 0 && channel <= 255

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
              <label className="text-xs font-medium text-muted-foreground">Blind Address</label>
              <input
                type="text"
                value={blindAddr}
                onChange={(e) => setBlindAddr((e.target as HTMLInputElement).value)}
                placeholder="0x313238"
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              />
            </div>
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">Remote Address</label>
              <input
                type="text"
                value={remoteAddr}
                onChange={(e) => setRemoteAddr((e.target as HTMLInputElement).value)}
                placeholder="0x17a753"
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              />
            </div>
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">Channel</label>
              <input
                type="number"
                value={channel}
                onChange={(e) => setChannel(parseInt((e.target as HTMLInputElement).value) || 0)}
                min="0"
                max="255"
                className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
              />
            </div>
            <div className="flex flex-col gap-1.5">
              <label className="text-xs font-medium text-muted-foreground">Command</label>
              <select
                value={command}
                onChange={(e) => setCommand((e.target as HTMLSelectElement).value)}
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
            onClick={() => setShowAdvanced(!showAdvanced)}
            className="text-xs text-muted-foreground hover:text-foreground"
          >
            {showAdvanced ? '- Hide advanced fields' : '+ Show advanced fields'}
          </button>

          {/* Advanced fields */}
          {showAdvanced && (
            <div className="grid grid-cols-2 gap-4 sm:grid-cols-5">
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">payload_1</label>
                <input
                  type="text"
                  value={payload1}
                  onChange={(e) => setPayload1((e.target as HTMLInputElement).value)}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">payload_2</label>
                <input
                  type="text"
                  value={payload2}
                  onChange={(e) => setPayload2((e.target as HTMLInputElement).value)}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">pck_inf1</label>
                <input
                  type="text"
                  value={pckInf1}
                  onChange={(e) => setPckInf1((e.target as HTMLInputElement).value)}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">pck_inf2</label>
                <input
                  type="text"
                  value={pckInf2}
                  onChange={(e) => setPckInf2((e.target as HTMLInputElement).value)}
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm focus:border-primary focus:outline-none focus:ring-1 focus:ring-primary"
                />
              </div>
              <div className="flex flex-col gap-1.5">
                <label className="text-xs font-medium text-muted-foreground">hop</label>
                <input
                  type="text"
                  value={hop}
                  onChange={(e) => setHop((e.target as HTMLInputElement).value)}
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
