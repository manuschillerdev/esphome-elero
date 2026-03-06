import { useState, useRef, useEffect } from 'preact/hooks'
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
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState(value)
  const inputRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (editing && inputRef.current) {
      inputRef.current.focus()
      inputRef.current.select()
    }
  }, [editing])

  useEffect(() => {
    setDraft(value)
  }, [value])

  const handleSave = () => {
    setEditing(false)
    if (draft !== value) {
      onSave(draft)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleSave()
    } else if (e.key === 'Escape') {
      setDraft(value)
      setEditing(false)
    }
  }

  if (editing) {
    return (
      <input
        ref={inputRef}
        type="text"
        value={draft}
        onChange={(e) => setDraft((e.target as HTMLInputElement).value)}
        onBlur={handleSave}
        onKeyDown={handleKeyDown}
        placeholder={placeholder}
        className={cn(
          'bg-transparent border-b border-primary outline-none',
          'text-inherit font-inherit',
          inputClassName
        )}
        style={{ width: `${Math.max(draft.length, 3)}ch` }}
      />
    )
  }

  return (
    <span
      onClick={() => setEditing(true)}
      className={cn(
        'cursor-pointer hover:bg-muted/50 rounded px-0.5 -mx-0.5 transition-colors',
        'border-b border-transparent hover:border-muted-foreground/30',
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
}

export function InlineEditNumber({
  value,
  onSave,
  suffix = '',
  className,
  min,
  max,
}: InlineEditNumberProps) {
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState(String(value))
  const inputRef = useRef<HTMLInputElement>(null)

  useEffect(() => {
    if (editing && inputRef.current) {
      inputRef.current.focus()
      inputRef.current.select()
    }
  }, [editing])

  useEffect(() => {
    setDraft(String(value))
  }, [value])

  const handleSave = () => {
    setEditing(false)
    const num = parseFloat(draft)
    if (!isNaN(num) && num !== value) {
      const clamped = Math.max(min ?? -Infinity, Math.min(max ?? Infinity, num))
      onSave(clamped)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter') {
      handleSave()
    } else if (e.key === 'Escape') {
      setDraft(String(value))
      setEditing(false)
    }
  }

  if (editing) {
    return (
      <span className="inline-flex items-center">
        <input
          ref={inputRef}
          type="number"
          value={draft}
          onChange={(e) => setDraft((e.target as HTMLInputElement).value)}
          onBlur={handleSave}
          onKeyDown={handleKeyDown}
          min={min}
          max={max}
          className={cn(
            'bg-transparent border-b border-primary outline-none',
            'text-inherit font-inherit w-16 [appearance:textfield]',
            '[&::-webkit-outer-spin-button]:appearance-none',
            '[&::-webkit-inner-spin-button]:appearance-none',
            className
          )}
        />
        {suffix && <span className="ml-0.5">{suffix}</span>}
      </span>
    )
  }

  return (
    <span
      onClick={() => setEditing(true)}
      className={cn(
        'cursor-pointer hover:bg-muted/50 rounded px-0.5 -mx-0.5 transition-colors',
        'border-b border-transparent hover:border-muted-foreground/30',
        className
      )}
    >
      {value}{suffix}
    </span>
  )
}
