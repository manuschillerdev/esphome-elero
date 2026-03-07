import { useRef } from 'preact/hooks'
import { useSignal, useSignalEffect } from '@preact/signals'
import { cn } from '@/lib/utils'

interface InlineEditProps {
  value: string
  onSave: (value: string) => void
  className?: string
  inputClassName?: string
  placeholder?: string
}

export function InlineEdit({
  value,
  onSave,
  className,
  inputClassName,
  placeholder,
}: InlineEditProps) {
  const editing = useSignal(false)
  const draft = useSignal(value)
  const inputRef = useRef<HTMLInputElement>(null)

  // Sync prop → draft when not editing (avoids stale draft after external update)
  if (!editing.value && draft.value !== value) {
    draft.value = value
  }

  // Auto-focus input when entering edit mode
  useSignalEffect(() => {
    if (editing.value && inputRef.current) {
      inputRef.current.focus()
      inputRef.current.select()
    }
  })

  const handleSave = () => {
    editing.value = false
    if (draft.value !== value) {
      onSave(draft.value)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleSave()
    } else if (e.key === 'Escape') {
      draft.value = value
      editing.value = false
    }
  }

  if (editing.value) {
    return (
      <input
        ref={inputRef}
        type="text"
        value={draft.value}
        onInput={(e) => { draft.value = (e.target as HTMLInputElement).value }}
        onBlur={handleSave}
        onKeyDown={handleKeyDown}
        placeholder={placeholder}
        className={cn(
          'bg-transparent border-b border-primary outline-none',
          'text-inherit font-inherit',
          inputClassName
        )}
        style={{ width: `${Math.max(draft.value.length, 3)}ch` }}
      />
    )
  }

  return (
    <span
      role="button"
      tabIndex={0}
      onClick={() => { editing.value = true }}
      onKeyDown={(e: KeyboardEvent) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); editing.value = true } }}
      className={cn(
        'hover:bg-muted/50 focus:bg-muted/50 rounded px-0.5 -mx-0.5 transition-colors',
        'border-b border-transparent hover:border-muted-foreground/30 focus:border-muted-foreground/30',
        'outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-1',
        className
      )}
    >
      {value || <span className="text-muted-foreground/50">{placeholder}</span>}
    </span>
  )
}

interface InlineEditNumberProps {
  value: number
  onSave: (value: number) => void
  suffix?: string
  className?: string
  min?: number
  max?: number
  step?: number
}

export function InlineEditNumber({
  value,
  onSave,
  suffix = '',
  className,
  min,
  max,
  step,
}: InlineEditNumberProps) {
  const editing = useSignal(false)
  const draft = useSignal(String(value))
  const inputRef = useRef<HTMLInputElement>(null)

  // Sync prop → draft when not editing
  if (!editing.value && draft.value !== String(value)) {
    draft.value = String(value)
  }

  // Auto-focus input when entering edit mode
  useSignalEffect(() => {
    if (editing.value && inputRef.current) {
      inputRef.current.focus()
      inputRef.current.select()
    }
  })

  const handleSave = () => {
    editing.value = false
    const num = parseFloat(draft.value)
    if (!isNaN(num) && num !== value) {
      const clamped = Math.max(min ?? -Infinity, Math.min(max ?? Infinity, num))
      onSave(clamped)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleSave()
    } else if (e.key === 'Escape') {
      draft.value = String(value)
      editing.value = false
    }
  }

  const displayWidth = `${Math.max(String(value).length, 1) + (suffix ? suffix.length : 0)}ch`

  if (editing.value) {
    return (
      <span className="inline-flex items-baseline" style={{ minWidth: displayWidth }}>
        <input
          ref={inputRef}
          type="number"
          value={draft.value}
          onInput={(e) => { draft.value = (e.target as HTMLInputElement).value }}
          onBlur={handleSave}
          onKeyDown={handleKeyDown}
          min={min}
          max={max}
          step={step ?? 'any'}
          className={cn(
            'bg-transparent border-b border-primary outline-none',
            'text-inherit font-inherit tabular-nums [appearance:textfield]',
            '[&::-webkit-outer-spin-button]:appearance-none',
            '[&::-webkit-inner-spin-button]:appearance-none',
            className
          )}
          style={{ width: `${Math.max(draft.value.length, 1)}ch` }}
        />
        {suffix && <span>{suffix}</span>}
      </span>
    )
  }

  return (
    <span
      role="button"
      tabIndex={0}
      onClick={() => { editing.value = true }}
      onKeyDown={(e: KeyboardEvent) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); editing.value = true } }}
      className={cn(
        'inline-flex items-baseline rounded px-0.5 -mx-0.5 transition-colors tabular-nums',
        'hover:bg-muted/50 focus:bg-muted/50 border-b border-transparent hover:border-muted-foreground/30 focus:border-muted-foreground/30',
        'outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-1',
        className
      )}
      style={{ minWidth: displayWidth }}
    >
      {value}{suffix}
    </span>
  )
}
