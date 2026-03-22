import { useSignal, useComputed } from '@preact/signals'
import { Card } from './ui/card'
import { Badge } from './ui/badge'
import { Button } from './ui/button'
import { Tooltip, TooltipTrigger, TooltipContent } from './ui/tooltip'
import { Send, Info } from './icons'
import { RfPackets } from './rf-packets'
import { cn } from '@/lib/utils'
import { hub, radio, devices, filterCounts, parseFreq } from '@/store'
import { sendRawCommand } from '@/ws'

// ─── Frequency Presets ──────────────────────────────────────────────────────

const FREQ_PRESETS = [
  { label: '868 MHz EU (Elero default)', freq2: 0x21, freq1: 0x71, freq0: 0x7a },
  { label: '868.95 MHz EU (Elero alt)',  freq2: 0x21, freq1: 0x71, freq0: 0xc0 },
  { label: '433.92 MHz ISM',             freq2: 0x10, freq1: 0xb1, freq0: 0x3b },
  { label: '315.00 MHz',                 freq2: 0x0c, freq1: 0x1d, freq0: 0x89 },
  { label: '903.00 MHz US ISM',          freq2: 0x22, freq1: 0xb6, freq0: 0x27 },
] as const

// ─── Command Presets ────────────────────────────────────────────────────────

const COMMAND_PRESETS = [
  { label: 'CHECK', value: '0x00', desc: 'Request status' },
  { label: 'STOP', value: '0x10', desc: 'Stop movement / stop dimming' },
  { label: 'UP / ON', value: '0x20', desc: 'Open blind / turn light on / dim up' },
  { label: 'TILT', value: '0x24', desc: 'Tilt position' },
  { label: 'DOWN / OFF', value: '0x40', desc: 'Close blind / turn light off / dim down' },
  { label: 'INT', value: '0x44', desc: 'Intermediate position' },
] as const

// ─── Helpers ────────────────────────────────────────────────────────────────

function calcFreqMHz(f2: number, f1: number, f0: number): string {
  return ((f2 * 256 * 256 + f1 * 256 + f0) * 26000000 / (1 << 16) / 1000000).toFixed(3)
}

const isValidHex = (s: string) => /^0x[0-9a-fA-F]+$/.test(s)

// ─── Hub Info Card ──────────────────────────────────────────────────────────

function HubInfoCard() {
  const { device, freq } = hub.value
  const counts = filterCounts.value

  const f2 = parseFreq(freq.freq2, 0x21)
  const f1 = parseFreq(freq.freq1, 0x71)
  const f0 = parseFreq(freq.freq0, 0x7a)
  const calculatedFreq = `${calcFreqMHz(f2, f1, f0)} MHz`

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex items-center justify-between border-b border-border px-5 py-4">
        <div>
          <h2 className="text-sm font-semibold text-card-foreground">Hub Information</h2>
          <p className="text-xs text-muted-foreground">Device configuration</p>
        </div>
      </div>
      <div className="grid grid-cols-2 gap-4 p-5 sm:grid-cols-4">
        <Stat label="Device" value={device || '-'} />
        <Stat label="Covers" value={String(counts.covers)} />
        <Stat label="Lights" value={String(counts.lights)} />
        <Stat label="Frequency" value={calculatedFreq} />
      </div>
    </Card>
  )
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex flex-col gap-1">
      <span className="text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
        {label}
      </span>
      <span className="text-sm font-medium">{value}</span>
    </div>
  )
}

// ─── Frequency Card ─────────────────────────────────────────────────────────

function FrequencyCard() {
  const freq = radio.value.freq
  const configF2 = parseFreq(freq.freq2, 0x21)
  const configF1 = parseFreq(freq.freq1, 0x71)
  const configF0 = parseFreq(freq.freq0, 0x7a)

  const selectedIdx = useSignal(-1) // -1 = use config values
  const active = selectedIdx.value >= 0 ? FREQ_PRESETS[selectedIdx.value] : null
  const f0 = active?.freq0 ?? configF0
  const f1 = active?.freq1 ?? configF1
  const f2 = active?.freq2 ?? configF2
  const calculatedFreq = `${calcFreqMHz(f2, f1, f0)} MHz`
  const isChanged = active && (f0 !== configF0 || f1 !== configF1 || f2 !== configF2)

  // Match config against presets for the dropdown default
  const configPresetIdx = FREQ_PRESETS.findIndex(
    (p) => p.freq2 === configF2 && p.freq1 === configF1 && p.freq0 === configF0
  )
  const dropdownValue = selectedIdx.value >= 0 ? String(selectedIdx.value) : (configPresetIdx >= 0 ? String(configPresetIdx) : '')

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex items-center justify-between border-b border-border px-5 py-4">
        <div>
          <h2 className="text-sm font-semibold text-card-foreground">CC1101 Frequency</h2>
          <p className="text-xs text-muted-foreground">Select a frequency preset to apply at runtime</p>
        </div>
        <Badge variant="secondary" className="font-mono text-xs">{calculatedFreq}</Badge>
      </div>

      {/* Preset dropdown */}
      <div className="px-5 py-4">
        <label className="mb-1.5 block text-[11px] font-medium uppercase tracking-wider text-muted-foreground">
          Frequency Preset
        </label>
        <select
          value={dropdownValue}
          onChange={(e) => {
            const val = (e.target as HTMLSelectElement).value
            selectedIdx.value = val === '' ? -1 : parseInt(val)
          }}
          className="h-8 w-full max-w-sm appearance-none rounded-md border border-input bg-background bg-[length:16px_16px] bg-[right_4px_center] bg-no-repeat pl-3 pr-6 text-xs shadow-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]"
          style={{ backgroundImage: selectChevronBg }}
        >
          {configPresetIdx < 0 && <option value="">Custom ({calcFreqMHz(configF2, configF1, configF0)} MHz)</option>}
          {FREQ_PRESETS.map((preset, i) => {
            const freqStr = calcFreqMHz(preset.freq2, preset.freq1, preset.freq0)
            return (
              <option key={i} value={String(i)}>
                {preset.label} — {freqStr} MHz
              </option>
            )
          })}
        </select>
        {isChanged && (
          <p className="mt-2 text-[11px] text-amber-600 dark:text-amber-400">
            Runtime frequency change not yet supported — requires a server-side handler for <code className="rounded bg-muted px-1">reinit_frequency()</code>.
          </p>
        )}
      </div>

      {/* Register values */}
      <div className="grid grid-cols-3 gap-4 border-t border-border px-5 py-4">
        <FreqRegister name="FREQ0" reg="0x0F" value={f0} />
        <FreqRegister name="FREQ1" reg="0x0E" value={f1} />
        <FreqRegister name="FREQ2" reg="0x0D" value={f2} />
      </div>
    </Card>
  )
}

function FreqRegister({ name, reg, value }: { name: string; reg: string; value: number }) {
  return (
    <div className="flex flex-col gap-1.5">
      <div className="flex items-center gap-2">
        <span className="text-xs font-medium text-foreground">{name}</span>
        <Badge variant="secondary" className="px-1.5 py-0 font-mono text-[10px]">{reg}</Badge>
      </div>
      <code className="h-8 flex items-center rounded-md border border-input bg-muted/50 px-2 font-mono text-sm text-muted-foreground">
        0x{value.toString(16).padStart(2, '0')}
      </code>
    </div>
  )
}

// ─── Raw TX Card ────────────────────────────────────────────────────────────

const selectChevronBg = "url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 24 24' fill='none' stroke='%236b7280' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E\")"
const selectClass = 'h-8 w-full appearance-none rounded-md border border-input bg-transparent bg-[length:16px_16px] bg-[right_4px_center] bg-no-repeat pl-2 pr-6 text-xs shadow-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]'
const inputClass = 'h-8 w-full rounded-md border border-input bg-transparent px-2 font-mono text-sm shadow-xs outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]'
const thClass = 'px-3 py-2 text-left text-[11px] font-medium uppercase tracking-wider text-muted-foreground'
const tdClass = 'px-3 py-1.5'

const FIELD_DESCRIPTIONS: Record<string, string> = {
  dst_address: '3-byte destination address of the blind or light motor (e.g. 0xa831e5)',
  src_address: '3-byte source address of the paired remote control (e.g. 0x17a753)',
  channel: 'RF channel number (0-255) the blind listens on',
  command: 'Command byte: CHECK (0x00), STOP (0x10), UP (0x20), TILT (0x24), DOWN (0x40), INT (0x44)',
  payload_1: 'First payload byte — device-specific, usually 0x00',
  payload_2: 'Second payload byte — device-specific, usually 0x04',
  type: 'Message type: COMMAND (0x6a), COMMAND_ALT (0x69), BUTTON (0x44), STATUS (0xca/0xc9)',
  type2: 'Secondary type byte — usually 0x00',
  hop: 'Hop counter — decremented on retransmit, default 0x0a',
}

function FieldLabel({ name }: { name: string }) {
  const desc = FIELD_DESCRIPTIONS[name]
  if (!desc) return <span>{name}</span>
  return (
    <Tooltip>
      <TooltipTrigger>
        <span className="inline-flex cursor-help items-center gap-1.5">
          {name}
          <Info className="size-3 text-muted-foreground/50" />
        </span>
      </TooltipTrigger>
      <TooltipContent className="left-full top-1/2 -translate-y-1/2 translate-x-0 bottom-auto ml-2 mb-0 w-max max-w-xs whitespace-normal">
        {desc}
      </TooltipContent>
    </Tooltip>
  )
}

const PAYLOAD_DEFAULTS = { payload_1: '0x00', payload_2: '0x04', type: '0x6a', type2: '0x00', hop: '0x0a' }

function PacketSummary({
  dstAddr, srcAddr, channel, command: cmd,
  payload1, payload2, type: msgType, type2, hop,
  dstOptions, srcOptions,
}: {
  dstAddr: string
  srcAddr: string
  channel: number
  command: string
  payload1: string
  payload2: string
  type: string
  type2: string
  hop: string
  dstOptions: Array<{ label: string; address: string; channel: number; kind: 'Blind' | 'Light' }>
  srcOptions: Array<{ label: string; address: string }>
}) {
  const cmdPreset = COMMAND_PRESETS.find((p) => p.value === cmd)
  const isCustomCmd = !cmdPreset
  const dst = dstOptions.find((d) => d.address === dstAddr)
  const src = srcOptions.find((s) => s.address === srcAddr)
  const dstName = dst ? `${dst.label} (${dst.address})` : dstAddr
  const srcName = src ? `${src.label !== src.address ? `${src.label} (${src.address})` : src.address}` : srcAddr
  const dstType = dst ? dst.kind : 'Device'

  const overrides = [
    payload1 !== PAYLOAD_DEFAULTS.payload_1 && { key: 'payload_1', val: payload1 },
    payload2 !== PAYLOAD_DEFAULTS.payload_2 && { key: 'payload_2', val: payload2 },
    msgType !== PAYLOAD_DEFAULTS.type && { key: 'type', val: msgType },
    type2 !== PAYLOAD_DEFAULTS.type2 && { key: 'type2', val: type2 },
    hop !== PAYLOAD_DEFAULTS.hop && { key: 'hop', val: hop },
  ].filter((v): v is { key: string; val: string } => !!v)

  return (
    <div className="rounded-md bg-primary/5 px-3 py-2 text-xs leading-relaxed">
      <p className="text-foreground">
        Send <span className="font-semibold text-emerald-700 dark:text-emerald-400">Command</span>{' '}{isCustomCmd
          ? <span className="font-semibold text-orange-600 dark:text-orange-400">{cmd}</span>
          : <span className="font-semibold text-primary">{cmdPreset.label} ({cmd})</span>
        }{' '}
        from <span className="font-semibold text-emerald-700 dark:text-emerald-400">Simulated Remote</span>{' '}
        <span className="font-semibold text-primary">{srcName}</span>{' '}
        to <span className="font-semibold text-emerald-700 dark:text-emerald-400">{dstType}</span>{' '}
        <span className="font-semibold text-primary">{dstName}</span>{' '}
        on Channel <span className="font-mono font-semibold text-primary">{channel}</span>
        {overrides.length > 0 ? (
          <>
            {' '}with parameter overrides{' '}
            {overrides.map((o, i) => (
              <span key={o.key}>
                {i > 0 && ', '}
                <span className="font-semibold text-orange-600 dark:text-orange-400">{o.key}={o.val}</span>
              </span>
            ))}
          </>
        ) : (
          <>{' '}with no parameter overrides</>
        )}
      </p>
      <p className="mt-1 font-mono text-[10px] text-muted-foreground">
        src_address={srcAddr} dst_address={dstAddr} channel={channel} command={cmd} payload_1={payload1} payload_2={payload2} type={msgType} type2={type2} hop={hop}
      </p>
    </div>
  )
}

function RawTxCard() {
  const devs = devices.value

  // Build dst/src options from device map
  const dstOptions: Array<{ label: string; address: string; channel: number; kind: 'Blind' | 'Light' }> = []
  const srcMap = new Map<string, string>()
  for (const d of devs.values()) {
    if (d.type === 'cover') dstOptions.push({ label: d.name, address: d.address, channel: d.channel, kind: 'Blind' })
    else if (d.type === 'light') dstOptions.push({ label: d.name, address: d.address, channel: d.channel, kind: 'Light' })
    else if (d.type === 'remote') srcMap.set(d.address, d.name)
  }
  const srcOptions = [...srcMap].map(([address, name]) => ({ address, label: name }))

  // Form state
  const dstAddr = useSignal('0x')
  const srcAddr = useSignal('0x')
  const channel = useSignal(1)
  const command = useSignal('0x00')
  const payload1 = useSignal('0x00')
  const payload2 = useSignal('0x04')
  const type = useSignal('0x6a')
  const type2 = useSignal('0x00')
  const hop = useSignal('0x0a')
  const sent = useSignal(false)

  const isFormValid = useComputed(() =>
    isValidHex(dstAddr.value) &&
    isValidHex(srcAddr.value) &&
    isValidHex(command.value) &&
    isValidHex(payload1.value) &&
    isValidHex(payload2.value) &&
    isValidHex(type.value) &&
    isValidHex(type2.value) &&
    isValidHex(hop.value) &&
    channel.value >= 0 && channel.value <= 255
  )

  const handleSendRaw = () => {
    sendRawCommand({
      dst_address: dstAddr.value,
      src_address: srcAddr.value,
      channel: channel.value,
      command: command.value,
      payload_1: payload1.value,
      payload_2: payload2.value,
      msg_type: type.value,
      type2: type2.value,
      hop: hop.value,
    })
    sent.value = true
    setTimeout(() => { sent.value = false }, 1500)
  }

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex items-center justify-between border-b border-border px-5 py-4">
        <div>
          <h2 className="text-sm font-semibold text-card-foreground">Simulate Remote</h2>
          <p className="text-xs text-muted-foreground">Send a command as if pressing a physical remote</p>
        </div>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full">
          <thead>
            <tr className="border-b border-border bg-muted/30">
              <th className={thClass}>Field</th>
              <th className={thClass}>Value</th>
              <th className={thClass}>Preset</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-border">
            {/* src_address (from) */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="src_address" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={srcAddr.value}
                  onInput={(e) => { srcAddr.value = (e.target as HTMLInputElement).value }}
                  placeholder="0x17a753"
                  className={inputClass}
                />
              </td>
              <td className={tdClass}>
                <select
                  value={srcOptions.some((s) => s.address === srcAddr.value) ? srcAddr.value : ''}
                  onChange={(e) => {
                    const addr = (e.target as HTMLSelectElement).value
                    if (addr) srcAddr.value = addr
                  }}
                  className={selectClass}
                  style={{ backgroundImage: selectChevronBg }}
                >
                  <option value="" disabled>from config...</option>
                  {srcOptions.map((s) => (
                    <option key={s.address} value={s.address}>{s.label !== s.address ? `${s.label} (${s.address})` : s.address}</option>
                  ))}
                </select>
              </td>
            </tr>

            {/* dst_address (to) */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="dst_address" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={dstAddr.value}
                  onInput={(e) => { dstAddr.value = (e.target as HTMLInputElement).value }}
                  placeholder="0x313238"
                  className={inputClass}
                />
              </td>
              <td className={tdClass}>
                <select
                  value={dstOptions.some((d) => d.address === dstAddr.value) ? dstAddr.value : ''}
                  onChange={(e) => {
                    const addr = (e.target as HTMLSelectElement).value
                    const dev = dstOptions.find((d) => d.address === addr)
                    if (dev) { dstAddr.value = dev.address; channel.value = dev.channel }
                  }}
                  className={selectClass}
                  style={{ backgroundImage: selectChevronBg }}
                >
                  <option value="" disabled>from config...</option>
                  {dstOptions.map((d) => (
                    <option key={d.address} value={d.address}>{d.label} ({d.address})</option>
                  ))}
                </select>
              </td>
            </tr>

            {/* channel */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="channel" /></td>
              <td className={tdClass}>
                <input
                  type="number"
                  value={channel.value}
                  onInput={(e) => { channel.value = parseInt((e.target as HTMLInputElement).value) || 0 }}
                  min={0}
                  max={255}
                  className={inputClass}
                />
              </td>
              <td className={cn(tdClass, 'text-[10px] text-muted-foreground/60')}>set by destination</td>
            </tr>

            {/* command */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="command" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={command.value}
                  onInput={(e) => { command.value = (e.target as HTMLInputElement).value }}
                  className={inputClass}
                />
              </td>
              <td className={tdClass}>
                <select
                  value={COMMAND_PRESETS.some((p) => p.value === command.value) ? command.value : ''}
                  onChange={(e) => {
                    const val = (e.target as HTMLSelectElement).value
                    if (val) command.value = val
                  }}
                  className={selectClass}
                  style={{ backgroundImage: selectChevronBg }}
                >
                  <option value="" disabled>from presets...</option>
                  {COMMAND_PRESETS.map((p) => (
                    <option key={p.value} value={p.value}>{p.label} ({p.value})</option>
                  ))}
                </select>
              </td>
            </tr>

            {/* payload_1 */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="payload_1" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={payload1.value}
                  onInput={(e) => { payload1.value = (e.target as HTMLInputElement).value }}
                  className={inputClass}
                />
              </td>
              <td />
            </tr>

            {/* payload_2 */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="payload_2" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={payload2.value}
                  onInput={(e) => { payload2.value = (e.target as HTMLInputElement).value }}
                  className={inputClass}
                />
              </td>
              <td />
            </tr>

            {/* type */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="type" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={type.value}
                  onInput={(e) => { type.value = (e.target as HTMLInputElement).value }}
                  className={inputClass}
                />
              </td>
              <td />
            </tr>

            {/* type2 */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="type2" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={type2.value}
                  onInput={(e) => { type2.value = (e.target as HTMLInputElement).value }}
                  className={inputClass}
                />
              </td>
              <td />
            </tr>

            {/* hop */}
            <tr>
              <td className={cn(tdClass, 'whitespace-nowrap text-xs font-medium text-foreground')}><FieldLabel name="hop" /></td>
              <td className={tdClass}>
                <input
                  type="text"
                  value={hop.value}
                  onInput={(e) => { hop.value = (e.target as HTMLInputElement).value }}
                  className={inputClass}
                />
              </td>
              <td />
            </tr>
          </tbody>
        </table>
      </div>

      {/* Footer — summary + send */}
      <div className="flex flex-col gap-3 border-t border-border bg-muted/30 px-5 py-3">
        <PacketSummary
          dstAddr={dstAddr.value}
          srcAddr={srcAddr.value}
          channel={channel.value}
          command={command.value}
          payload1={payload1.value}
          payload2={payload2.value}
          type={type.value}
          type2={type2.value}
          hop={hop.value}
          dstOptions={dstOptions}
          srcOptions={srcOptions}
        />
        <div className="flex items-center justify-end">
          <Button
            onClick={handleSendRaw}
            disabled={!isFormValid.value}
            className={cn('gap-2', sent.value && 'bg-green-600 hover:bg-green-600')}
          >
            <Send className="size-4" />
            {sent.value ? 'Sent!' : 'Send'}
          </Button>
        </div>
      </div>
    </Card>
  )
}

// ─── Hub Panel (main export) ────────────────────────────────────────────────

export function HubPanel() {
  return (
    <div className="flex flex-col gap-6">
      <HubInfoCard />
      <FrequencyCard />
      <RawTxCard />
      <RfPackets />
    </div>
  )
}
