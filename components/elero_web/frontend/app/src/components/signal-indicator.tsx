import { cn } from '@/lib/utils'

type SignalStrength = 'excellent' | 'good' | 'fair' | 'weak'

function getSignalStrength(rssi: number): SignalStrength {
  if (rssi >= -50) return 'excellent'
  if (rssi >= -70) return 'good'
  if (rssi >= -80) return 'fair'
  return 'weak'
}

function getSignalColor(strength: SignalStrength): string {
  switch (strength) {
    case 'excellent':
      return 'text-success'
    case 'good':
      return 'text-success'
    case 'fair':
      return 'text-warning'
    case 'weak':
      return 'text-destructive'
  }
}

export function SignalIndicator({ rssi }: { rssi: number }) {
  const strength = getSignalStrength(rssi)
  const color = getSignalColor(strength)
  const bars = strength === 'excellent' ? 4 : strength === 'good' ? 3 : strength === 'fair' ? 2 : 1

  return (
    <div
      className="flex items-end gap-0.5"
      role="img"
      aria-label={`Signal strength: ${rssi} dBm (${strength})`}
    >
      {[1, 2, 3, 4].map((bar) => (
        <div
          key={bar}
          className={cn(
            'w-[3px] rounded-full transition-colors',
            bar <= bars ? color : 'bg-border',
            bar <= bars ? 'bg-current' : ''
          )}
          style={{ height: `${bar * 3 + 4}px` }}
        />
      ))}
    </div>
  )
}
