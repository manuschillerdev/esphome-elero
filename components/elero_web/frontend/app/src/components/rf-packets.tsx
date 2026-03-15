import { Card } from './ui/card'
import { Trash2 } from './icons'
import { buildFullColumns } from './packet-table'
import { rfPackets, displayNames, deviceTypeMap, clearRfPackets } from '@/store'
import { DataTable } from './ui/data-table'

export function RfPackets() {
  const packets = rfPackets.value
  const names = displayNames.value
  const types = deviceTypeMap.value
  const columns = buildFullColumns(names, types)
  const data = packets.slice(-50).reverse()

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
            onClick={clearRfPackets}
          >
            <Trash2 className="size-3" />
            Clear
          </button>
        }
      />
    </Card>
  )
}
