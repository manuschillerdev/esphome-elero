import { useSignal } from '@preact/signals'
import { useId } from 'preact/hooks'
import { command, updateDevice, type Device } from '@/store'
import { Button } from './ui/button'

const fields = [
  { key: 'command_up', label: 'Open', defaultValue: command.UP },
  { key: 'command_down', label: 'Close', defaultValue: command.DOWN },
  { key: 'command_stop', label: 'Stop', defaultValue: command.STOP },
  { key: 'command_check', label: 'Status query', defaultValue: command.CHECK },
  { key: 'command_tilt', label: 'Saved tilt', defaultValue: command.TILT },
] as const

type CommandField = typeof fields[number]

function CommandByteInput({ device, field }: { device: Device; field: CommandField }) {
  const value = device[field.key]
  const draft = useSignal(value)
  const editing = useSignal(false)
  const invalid = useSignal(false)
  const id = useId()

  if (!editing.value && draft.value !== value) draft.value = value

  function commit() {
    const text = draft.value.trim()
    if (/^(?:0x)?[\da-f]{1,2}$/i.test(text)) {
      const byte = `0x${parseInt(text.replace(/^0x/i, ''), 16).toString(16).padStart(2, '0')}`
      if (byte !== value) updateDevice(device.address, { [field.key]: byte })
      invalid.value = false
    } else {
      invalid.value = true
    }
    editing.value = false
  }

  return (
    <div className="space-y-1">
      <label htmlFor={id} className="block text-xs text-muted-foreground">{field.label} command</label>
      <input
        id={id}
        type="text"
        value={draft.value}
        spellcheck={false}
        autoComplete="off"
        aria-invalid={invalid.value}
        aria-describedby={invalid.value ? `${id}-error` : undefined}
        onFocus={() => { editing.value = true }}
        onInput={(event) => { draft.value = event.currentTarget.value; invalid.value = false }}
        onBlur={commit}
        onKeyDown={(event) => {
          if (event.key === 'Enter') {
            event.preventDefault()
            event.currentTarget.blur()
          } else if (event.key === 'Escape') {
            draft.value = value
            event.currentTarget.blur()
          }
        }}
        className="h-8 w-24 rounded-md border border-input bg-background px-2 font-mono text-xs outline-none focus-visible:ring-2 focus-visible:ring-ring aria-invalid:border-destructive"
      />
      {invalid.value && <p id={`${id}-error`} role="alert" className="text-xs text-destructive">Use 0x00–0xFF. Previous value kept.</p>}
    </div>
  )
}

export function CoverCommandEditor({ device, disabled }: { device: Device; disabled: boolean }) {
  return (
    <details className="border-b border-border px-5 py-3">
      <summary className="cursor-pointer text-xs font-medium">Custom command bytes</summary>
      <fieldset disabled={disabled} className="mt-3 space-y-3 disabled:opacity-50">
        <legend className="sr-only">Custom cover command bytes</legend>
        <p className="text-xs text-muted-foreground">
          Use hex values from your receiver setup. Save to NVS to apply changes.
          These values do not change the Short press or Full travel buttons.
        </p>
        {device.movement_mode === 'step' && (
          <p className="text-xs text-muted-foreground">
            Step receiver mode overrides Open and Close with 0x21 and 0x41.
            Select Standard to use your custom Open and Close values.
          </p>
        )}
        <div className="flex flex-wrap gap-4">
          {fields.map((field) => <CommandByteInput key={field.key} device={device} field={field} />)}
        </div>
        <Button type="button" variant="outline" size="sm" onClick={() => updateDevice(device.address,
          Object.fromEntries(fields.map((field) => [field.key, field.defaultValue])),
        )}>Reset command bytes</Button>
        <p className="text-xs text-muted-foreground">Reset restores 0x20, 0x40, 0x10, 0x00, and 0x24. Save to NVS to apply.</p>
      </fieldset>
    </details>
  )
}
