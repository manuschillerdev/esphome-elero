import { cn } from '@/lib/utils'
import { radio } from '@/store'

type SignalStrength = 'excellent' | 'good' | 'fair' | 'weak'

// Derive thresholds from receiver sensitivity.
// Usable range spans ~30 dB above sensitivity (below that = unreliable).
// Split: excellent (top 10 dB), good (next 10 dB), fair (next 10 dB), weak (rest).
//   CC1101 (-104): excellent >= -74, good >= -84, fair >= -94
//   SX1262 (-117): excellent >= -87, good >= -97, fair >= -107
function getSignalStrength(rssi: number, sensitivity: number): SignalStrength {
  if (rssi >= sensitivity + 30) return 'excellent'
  if (rssi >= sensitivity + 20) return 'good'
  if (rssi >= sensitivity + 10) return 'fair'
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

export function SignalIndicator({ rssi }: { rssi: number | null | undefined }) {
  if (rssi == null || rssi === 0) {
    return (
      <div
        className="flex items-end gap-0.5"
        role="img"
        aria-label="Signal strength: unknown"
      >
        {[1, 2, 3, 4].map((bar) => (
          <div
            key={bar}
            className="w-[3px] rounded-full bg-border"
            style={{ height: `${bar * 3 + 4}px` }}
          />
        ))}
      </div>
    )
  }

  const sensitivity = radio.value.rx_sensitivity
  const strength = getSignalStrength(rssi, sensitivity)
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
