import { Card } from './ui/card'
import { Trash2 } from './icons'
import { buildFullColumns } from './packet-table'
import { useStore, buildConfigNames, buildAddressTypes } from '@/store'
import { DataTable } from './ui/data-table'

export function RfPackets() {
  const rfPackets = useStore((s) => s.rfPackets)
  const blinds = useStore((s) => s.config.blinds)
  const lights = useStore((s) => s.config.lights)
  const remoteNames = useStore((s) => s.remoteNames)

  const configNames = buildConfigNames(blinds, lights, remoteNames)
  const addressTypes = buildAddressTypes(blinds, lights, rfPackets)
  const columns = buildFullColumns(configNames, addressTypes)
  const data = rfPackets.slice(-50).reverse()

  return (
    <Card className="gap-0 overflow-hidden p-0">
      <div className="flex items-center justify-between border-b border-border px-5 py-4">
        <div>
          <h2 className="text-sm font-semibold text-card-foreground">RF Packets</h2>
          <p className="text-xs text-muted-foreground">Live RF traffic from all devices and remotes</p>
        </div>
      </div>
      <DataTable
        columns={columns}
        data={data}
        rowKey={(pkt, i) => `${pkt.t}-${i}`}
        defaultSort={{ key: 'time', direction: 'desc' }}
        maxHeight="500px"
        tableClass="font-mono"
        emptyMessage="No packets received yet — operate a physical remote or use Simulate Remote above to see traffic. If nothing appears, check your frequency and pin configuration."
        toolbar={
          <button
            className="inline-flex items-center gap-1.5 rounded-md border border-transparent px-2 py-1 text-[11px] font-medium text-muted-foreground hover:text-foreground hover:bg-accent transition-colors"
            onClick={() => useStore.getState().clearRfPackets()}
          >
            <Trash2 className="size-3" />
            Clear
          </button>
        }
      />
    </Card>
  )
}
