#pragma once

// Auto-generated from frontend/index.html
// Do not edit manually - run: python frontend/generate_header.py

namespace esphome {
namespace elero {

const char ELERO_WEB_UI_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Elero RF Bridge</title>
  <script src="https://cdn.tailwindcss.com"></script>
  <script defer src="https://cdn.jsdelivr.net/npm/alpinejs@3/dist/cdn.min.js"></script>
  <style>
    [x-cloak] { display: none !important; }
  </style>
</head>
<body class="bg-gray-100 min-h-screen p-4" x-data="eleroApp()" x-init="init()">

  <!-- Connection Status -->
  <div class="max-w-4xl mx-auto mb-4">
    <div class="flex items-center gap-2">
      <div class="w-3 h-3 rounded-full" :class="connected ? 'bg-green-500' : 'bg-red-500'"></div>
      <span class="text-sm text-gray-600" x-text="connected ? 'Connected' : 'Disconnected'"></span>
      <span class="text-sm text-gray-400" x-text="config.device || ''"></span>
    </div>
  </div>

  <!-- Configured Blinds -->
  <div class="max-w-4xl mx-auto mb-6">
    <h2 class="text-lg font-semibold mb-2">Configured Blinds</h2>
    <div class="space-y-2">
      <template x-for="blind in config.blinds" :key="blind.address">
        <div class="bg-white rounded-lg shadow p-4">
          <div class="flex items-center justify-between">
            <div>
              <div class="font-medium" x-text="blind.name"></div>
              <div class="text-xs text-gray-500">
                <span x-text="blind.address"></span> · ch <span x-text="blind.channel"></span>
              </div>
              <div class="text-xs text-gray-400 mt-1" x-show="blindState(blind.address)">
                <span x-text="'State: ' + (blindState(blind.address)?.state || 'unknown')"></span>
                <span x-text="' · RSSI: ' + (blindState(blind.address)?.rssi?.toFixed(1) || '-') + ' dBm'"></span>
              </div>
            </div>
            <div class="flex gap-2">
              <button @click="sendCmd(blind.address, 'up')"
                      class="px-3 py-1.5 text-sm bg-green-100 text-green-700 rounded hover:bg-green-200">Open</button>
              <button @click="sendCmd(blind.address, 'stop')"
                      class="px-3 py-1.5 text-sm bg-gray-100 text-gray-700 rounded hover:bg-gray-200">Stop</button>
              <button @click="sendCmd(blind.address, 'down')"
                      class="px-3 py-1.5 text-sm bg-red-100 text-red-700 rounded hover:bg-red-200">Close</button>
              <button x-show="blind.tilt" @click="sendCmd(blind.address, 'tilt')"
                      class="px-3 py-1.5 text-sm bg-blue-100 text-blue-700 rounded hover:bg-blue-200">Tilt</button>
            </div>
          </div>
        </div>
      </template>
      <div x-show="!config.blinds?.length" class="text-gray-500 text-sm">No blinds configured</div>
    </div>
  </div>

  <!-- Discovered (addresses seen but not configured) -->
  <div class="max-w-4xl mx-auto mb-6">
    <h2 class="text-lg font-semibold mb-2">Discovered</h2>
    <div class="space-y-2">
      <template x-for="addr in discoveredAddresses" :key="addr">
        <div class="bg-yellow-50 border border-yellow-200 rounded-lg p-4">
          <div class="flex items-center justify-between">
            <div>
              <div class="font-mono text-sm" x-text="addr"></div>
              <div class="text-xs text-gray-500">
                <span x-text="'Last seen: ' + formatTime(states[addr]?.t)"></span>
                <span x-text="' · RSSI: ' + (states[addr]?.rssi?.toFixed(1) || '-') + ' dBm'"></span>
                <span x-text="' · ch ' + (states[addr]?.ch || '?')"></span>
              </div>
            </div>
            <button @click="copyYaml(addr)" class="text-sm text-blue-600 hover:underline">Copy YAML</button>
          </div>
        </div>
      </template>
      <div x-show="!discoveredAddresses.length" class="text-gray-500 text-sm">No new devices discovered</div>
    </div>
  </div>

  <!-- RF Packets -->
  <div class="max-w-4xl mx-auto mb-6">
    <div class="flex items-center justify-between mb-2">
      <h2 class="text-lg font-semibold">RF Packets</h2>
      <div class="flex gap-2">
        <input type="text" x-model="rfFilter" placeholder="Filter by address..."
               class="px-2 py-1 text-sm border rounded w-40">
        <button @click="rfPackets = []" class="text-sm text-gray-500 hover:text-gray-700">Clear</button>
      </div>
    </div>
    <div class="bg-white rounded-lg shadow max-h-64 overflow-y-auto">
      <table class="w-full text-xs font-mono">
        <thead class="bg-gray-50 sticky top-0">
          <tr>
            <th class="px-2 py-1 text-left">Time</th>
            <th class="px-2 py-1 text-left">Src</th>
            <th class="px-2 py-1 text-left">Dst</th>
            <th class="px-2 py-1 text-left">Type</th>
            <th class="px-2 py-1 text-left">Cmd/State</th>
            <th class="px-2 py-1 text-left">RSSI</th>
          </tr>
        </thead>
        <tbody>
          <template x-for="pkt in filteredPackets" :key="pkt.t">
            <tr class="border-t hover:bg-gray-50">
              <td class="px-2 py-1" x-text="formatTime(pkt.t)"></td>
              <td class="px-2 py-1" x-text="pkt.src"></td>
              <td class="px-2 py-1" x-text="pkt.dst"></td>
              <td class="px-2 py-1" x-text="pkt.type"></td>
              <td class="px-2 py-1" x-text="pkt.type === '0x6a' ? pkt.cmd : pkt.state"></td>
              <td class="px-2 py-1" x-text="pkt.rssi?.toFixed(1)"></td>
            </tr>
          </template>
        </tbody>
      </table>
      <div x-show="!filteredPackets.length" class="p-4 text-center text-gray-500 text-sm">No packets</div>
    </div>
  </div>

  <!-- Logs -->
  <div class="max-w-4xl mx-auto">
    <div class="flex items-center justify-between mb-2">
      <h2 class="text-lg font-semibold">Logs</h2>
      <button @click="logs = []" class="text-sm text-gray-500 hover:text-gray-700">Clear</button>
    </div>
    <div class="bg-white rounded-lg shadow max-h-48 overflow-y-auto">
      <div class="p-2 space-y-1 font-mono text-xs">
        <template x-for="log in logs.slice(-50)" :key="log.t">
          <div :class="logClass(log.level)">
            <span class="text-gray-400" x-text="formatTime(log.t)"></span>
            <span x-text="'[' + log.tag + ']'"></span>
            <span x-text="log.msg"></span>
          </div>
        </template>
        <div x-show="!logs.length" class="text-gray-500">No logs</div>
      </div>
    </div>
  </div>

<script>
let _ws = null

function eleroApp() {
  return {
    connected: false,
    config: { blinds: [], lights: [], device: '', freq: {} },
    states: {},      // address -> last RF state
    rfPackets: [],   // RF packet log
    logs: [],        // ESPHome logs
    rfFilter: '',
    reconnectTimer: null,

    init() {
      this.connect()
    },

    connect() {
      if (_ws) {
        _ws.onopen = null
        _ws.onclose = null
        _ws.onerror = null
        _ws.onmessage = null
        _ws.close()
      }

      const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
      _ws = new WebSocket(`${proto}//${location.host}/elero/ws`)
      const ws = _ws

      ws.onopen = () => {
        this.connected = true
        if (this.reconnectTimer) {
          clearTimeout(this.reconnectTimer)
          this.reconnectTimer = null
        }
      }

      ws.onclose = () => {
        this.connected = false
        if (_ws === ws) {
          _ws = null
          this.reconnectTimer = setTimeout(() => this.connect(), 2000)
        }
      }

      ws.onerror = () => {
        this.connected = false
      }

      ws.onmessage = (e) => {
        const msg = JSON.parse(e.data)
        this.handleEvent(msg.event, msg.data)
      }
    },

    handleEvent(event, data) {
      if (event === 'config') {
        this.config = data
      } else if (event === 'rf') {
        // Update state for this address
        const addr = data.src
        this.states[addr] = data
        // Add to packet log
        this.rfPackets.push(data)
        if (this.rfPackets.length > 200) {
          this.rfPackets.shift()
        }
      } else if (event === 'log') {
        this.logs.push(data)
        if (this.logs.length > 100) {
          this.logs.shift()
        }
      }
    },

    sendCmd(address, action) {
      if (_ws && _ws.readyState === WebSocket.OPEN) {
        _ws.send(JSON.stringify({ type: 'cmd', address, action }))
      }
    },

    blindState(address) {
      return this.states[address]
    },

    get configuredAddresses() {
      const addrs = new Set()
      for (const b of this.config.blinds || []) addrs.add(b.address)
      for (const l of this.config.lights || []) addrs.add(l.address)
      return addrs
    },

    get discoveredAddresses() {
      const configured = this.configuredAddresses
      return Object.keys(this.states).filter(a => !configured.has(a))
    },

    get filteredPackets() {
      if (!this.rfFilter) return this.rfPackets.slice(-50).reverse()
      const f = this.rfFilter.toLowerCase()
      return this.rfPackets
        .filter(p => p.src.toLowerCase().includes(f) || p.dst.toLowerCase().includes(f))
        .slice(-50).reverse()
    },

    formatTime(ms) {
      if (!ms) return ''
      const s = Math.floor(ms / 1000)
      const m = Math.floor(s / 60)
      return `${m}:${String(s % 60).padStart(2, '0')}`
    },

    logClass(level) {
      if (level === 1) return 'text-red-600'
      if (level === 2) return 'text-yellow-600'
      return 'text-gray-700'
    },

    copyYaml(address) {
      const st = this.states[address]
      const yaml = `  - platform: elero
    blind_address: ${address}
    channel: ${st?.ch || 0}
    remote_address: ${st?.dst || '0x000000'}
    name: "New Blind"
    # open_duration: 25s
    # close_duration: 22s`
      navigator.clipboard.writeText(yaml)
      alert('YAML copied to clipboard')
    }
  }
}
</script>
</body>
</html>
)rawliteral";

}  // namespace elero
}  // namespace esphome
