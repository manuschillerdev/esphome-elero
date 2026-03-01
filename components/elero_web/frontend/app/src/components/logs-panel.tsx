import { useMemo, useState } from 'preact/hooks'
import { Card } from './ui/card'
import { Button } from './ui/button'
import { FileText, Trash2, ChevronDown } from './icons'
import { cn } from '@/lib/utils'
import { useStore } from '@/store'

function formatTime(ms: number): string {
  if (!ms) return ''
  const s = Math.floor(ms / 1000)
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`
}

export function LogsPanel() {
  const logs = useStore((s) => s.logs)
  const [tagFilter, setTagFilter] = useState('')
  const [levelFilter, setLevelFilter] = useState<'' | '1' | '2' | '3'>('')

  // Get unique tags
  const uniqueTags = useMemo(() => {
    const tags = new Set<string>()
    logs.forEach((log) => tags.add(log.tag))
    return Array.from(tags).sort()
  }, [logs])

  // Filter logs
  const filtered = useMemo(() => {
    let result = logs
    if (tagFilter) {
      result = result.filter((log) => log.tag === tagFilter)
    }
    if (levelFilter) {
      const level = parseInt(levelFilter)
      result = result.filter((log) => log.level <= level)
    }
    return result.slice(-50)
  }, [logs, tagFilter, levelFilter])

  const levelClass = (level: number) => {
    if (level === 1) return 'text-destructive'
    if (level === 2) return 'text-warning-foreground'
    return 'text-foreground'
  }

  const levelLabels = [
    { value: '', label: 'All levels' },
    { value: '1', label: 'Errors only' },
    { value: '2', label: 'Warnings+' },
    { value: '3', label: 'Info+' },
  ]

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex items-center justify-between border-b border-border px-5 py-4">
        <div className="flex items-center gap-3">
          <div className="flex size-8 items-center justify-center rounded-lg bg-muted text-muted-foreground">
            <FileText className="size-4" />
          </div>
          <div>
            <h2 className="text-sm font-semibold text-card-foreground">Logs</h2>
            <p className="text-xs text-muted-foreground">{logs.length} entries</p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          {/* Level filter */}
          <div className="flex items-center gap-1 rounded-lg bg-muted p-1">
            {levelLabels.map((l) => (
              <button
                key={l.value}
                onClick={() => setLevelFilter(l.value as '' | '1' | '2' | '3')}
                className={cn(
                  'rounded-md px-2 py-1 text-xs font-medium transition-colors',
                  levelFilter === l.value
                    ? 'bg-card text-card-foreground shadow-sm'
                    : 'text-muted-foreground hover:text-foreground'
                )}
              >
                {l.label}
              </button>
            ))}
          </div>

          {/* Tag filter */}
          <div className="relative">
            <select
              value={tagFilter}
              onChange={(e) => setTagFilter((e.target as HTMLSelectElement).value)}
              className={cn(
                'h-8 w-32 appearance-none rounded-md border border-input bg-background px-3 pr-8 text-xs',
                'focus:outline-none focus:ring-2 focus:ring-ring',
                !tagFilter && 'text-muted-foreground'
              )}
            >
              <option value="">All tags</option>
              {uniqueTags.map((tag) => (
                <option key={tag} value={tag}>
                  {tag}
                </option>
              ))}
            </select>
            <ChevronDown className="pointer-events-none absolute right-2 top-1/2 size-3.5 -translate-y-1/2 text-muted-foreground" />
          </div>

          <Button
            variant="outline"
            size="sm"
            className="gap-1.5 text-xs"
            onClick={() => useStore.getState().clearLogs()}
          >
            <Trash2 className="size-3" />
            Clear
          </Button>
        </div>
      </div>

      <div className="max-h-[500px] overflow-y-auto p-4">
        <div className="space-y-1 font-mono text-xs">
          {filtered.map((log, i) => (
            <div key={`${log.t}-${i}`} className={cn('flex gap-2', levelClass(log.level))}>
              <span className="text-muted-foreground shrink-0">{formatTime(log.t)}</span>
              <span className="text-muted-foreground shrink-0">[{log.tag}]</span>
              <span className="break-all">{log.msg}</span>
            </div>
          ))}
          {filtered.length === 0 && (
            <div className="py-8 text-center text-muted-foreground">No logs</div>
          )}
        </div>
      </div>
    </Card>
  )
}
