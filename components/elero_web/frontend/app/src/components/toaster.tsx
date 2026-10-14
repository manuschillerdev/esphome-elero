import { useSignalEffect } from '@preact/signals'
import { toast, dismissToast } from '@/store'
import { cn } from '@/lib/utils'
import { CheckCircle2, AlertCircle, Info } from './icons'

const VARIANT_STYLES = {
  success: 'border-emerald-500/40 bg-emerald-50 text-emerald-900 dark:bg-emerald-950/40 dark:text-emerald-200',
  error: 'border-red-500/40 bg-red-50 text-red-900 dark:bg-red-950/40 dark:text-red-200',
  info: 'border-border bg-card text-card-foreground',
} as const

const VARIANT_ICON = {
  success: CheckCircle2,
  error: AlertCircle,
  info: Info,
} as const

export function Toaster() {
  const current = toast.value

  // Auto-dismiss after 4s. Re-arm on every toast id change.
  useSignalEffect(() => {
    const t = toast.value
    if (!t) return
    const timer = setTimeout(() => {
      // Only dismiss if the toast hasn't been replaced in the meantime.
      if (toast.value?.id === t.id) dismissToast()
    }, 4000)
    return () => clearTimeout(timer)
  })

  if (!current) return null

  const Icon = VARIANT_ICON[current.variant]

  return (
    <div className="pointer-events-none fixed inset-x-0 bottom-4 z-50 flex justify-center px-4">
      <div
        className={cn(
          'pointer-events-auto flex max-w-md items-start gap-3 rounded-md border px-4 py-3 shadow-lg',
          VARIANT_STYLES[current.variant],
        )}
        role="status"
      >
        <Icon className="mt-0.5 size-4 shrink-0" />
        <p className="text-sm leading-snug">{current.message}</p>
        <button
          type="button"
          onClick={dismissToast}
          className="ml-2 text-xs uppercase tracking-wider opacity-70 hover:opacity-100"
          aria-label="Dismiss"
        >
          ×
        </button>
      </div>
    </div>
  )
}
