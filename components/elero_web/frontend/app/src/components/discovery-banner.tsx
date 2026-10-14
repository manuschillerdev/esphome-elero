import { Radio } from './icons'
import { configuredFrequency, connected } from '@/store'

export function DiscoveryBanner() {
  return (
    <div className="flex flex-col gap-1.5 rounded-xl border border-dashed border-border bg-muted/30 px-4 py-3">
      <div className="flex items-center gap-2">
        <span className="relative flex size-5 items-center justify-center">
          <span className="absolute inline-flex size-full animate-ping rounded-full bg-muted-foreground/20" />
          <Radio className="relative size-4 text-muted-foreground" />
        </span>
        <h3 className="text-xs font-semibold text-foreground">
          RF Discovery — {connected.value ? 'listening on' : 'configured for'} {configuredFrequency.value}
        </h3>
      </div>
      <p className="text-[11px] leading-relaxed text-muted-foreground">
        Press buttons on your physical Elero remotes. The UI captures live RF packets and shows
        new addresses below.
      </p>
    </div>
  )
}
