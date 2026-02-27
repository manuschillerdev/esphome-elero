import Alpine from 'alpinejs'

// ─── State label map (matches elero_state_to_string() in C++) ────────────────
const STATE_LABELS = {
  top:              'Top',
  bottom:           'Bottom',
  intermediate:     'Intermediate',
  tilt:             'Tilt',
  blocking:         'Blocking',
  overheated:       'Overheated',
  timeout:          'Timeout',
  start_moving_up:  'Starting Up',
  start_moving_down:'Starting Down',
  moving_up:        'Moving Up',
  moving_down:      'Moving Down',
  stopped:          'Stopped',
  top_tilt:         'Top (Tilt)',
  bottom_tilt:      'Bottom (Tilt)',
  unknown:          'Unknown',
  on:               'On',
  off:              'Off',
}

// ─── Utility ─────────────────────────────────────────────────────────────────
function relTime(ms) {
  if (!ms) return 'never'
  const diff = Math.floor((Date.now() - ms) / 1000)  // rough, millis() ≠ epoch
  if (diff < 5)   return 'just now'
  if (diff < 60)  return `${diff}s ago`
  if (diff < 3600)return `${Math.floor(diff/60)}m ago`
  return `${Math.floor(diff/3600)}h ago`
}

function fmtTs(ms) {
  const s = Math.floor(ms / 1000)
  const h = Math.floor(s / 3600) % 24
  const m = Math.floor(s / 60) % 60
  const sec = s % 60
  return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(sec).padStart(2,'0')}`
}

function uptimeFmt(ms) {
  if (!ms) return ''
  const s = Math.floor(ms / 1000)
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const sec = s % 60
  if (h > 0) return `${h}h ${m}m ${sec}s`
  if (m > 0) return `${m}m ${sec}s`
  return `${sec}s`
}

function rssiIcon(rssi) {
  if (rssi >= -65) return '▂▄█ ' + rssi.toFixed(1) + ' dBm'
  if (rssi >= -80) return '▂▄░ ' + rssi.toFixed(1) + ' dBm'
  return '▂░░ ' + rssi.toFixed(1) + ' dBm'
}

function genId() {
  return Math.random().toString(36).substring(2, 10)
}

// ─── Alpine app ──────────────────────────────────────────────────────────────
document.addEventListener('alpine:init', () => {
  Alpine.data('app', () => ({
    // Navigation
    tab: 'devices',

    // WebSocket
    ws: null,
    wsConnected: false,
    wsReconnectTimer: null,

    // Device info
    deviceName: '',
    uptimeMs: 0,
    get uptimeStr() { return uptimeFmt(this.uptimeMs) },

    // Covers (configured + adopted)
    covers: [],
    settingsOpen: null,   // blind_address of expanded settings panel

    // Discovery
    scanning: false,
    allDiscovered: [],
    get discoveredNew()   { return this.allDiscovered.filter(b => !b.already_configured && !b.already_adopted) },
    get discoveredKnown() { return this.allDiscovered.filter(b =>  b.already_configured ||  b.already_adopted) },

    // Adopt modal
    adoptTarget: null,
    adoptName: '',

    // YAML modal
    yamlContent: null,

    // Log
    logCapture: false,
    logLevel: '3',
    logAutoScroll: true,
    logEntries: [],
    get filteredLog() {
      return this.logEntries.filter(e => e.level <= parseInt(this.logLevel))
    },

    // Config — frequency
    freq: { freq2: '', freq1: '', freq0: '' },
    freqStatus: '',

    // Config — packet dump
    dumpActive: false,
    dumpPackets: [],

    // Toast
    toast: { show: false, error: false, msg: '' },
    _toastTimer: null,

    // Pending command callbacks
    _pendingCmds: {},

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    init() {
      this.connect()
    },

    // ── WebSocket connection ──────────────────────────────────────────────────
    connect() {
      if (this.ws && (this.ws.readyState === WebSocket.CONNECTING || this.ws.readyState === WebSocket.OPEN)) {
        return
      }

      const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:'
      this.ws = new WebSocket(`${protocol}//${location.host}/elero/ws`)

      this.ws.onopen = () => {
        this.wsConnected = true
        if (this.wsReconnectTimer) {
          clearTimeout(this.wsReconnectTimer)
          this.wsReconnectTimer = null
        }
      }

      this.ws.onclose = () => {
        this.wsConnected = false
        this.ws = null
        // Auto-reconnect after 2 seconds
        this.wsReconnectTimer = setTimeout(() => this.connect(), 2000)
      }

      this.ws.onerror = () => {
        // Will trigger onclose
      }

      this.ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data)
          this.handleMessage(msg)
        } catch (e) {
          console.error('WS message parse error:', e)
        }
      }
    },

    // ── Message handler ───────────────────────────────────────────────────────
    handleMessage(msg) {
      const { type, data, id, ok, error } = msg

      // Command result
      if (type === 'result') {
        if (id && this._pendingCmds[id]) {
          const { resolve, reject } = this._pendingCmds[id]
          delete this._pendingCmds[id]
          if (ok) {
            resolve()
          } else {
            reject(new Error(error || 'Command failed'))
          }
        }
        return
      }

      // YAML response
      if (type === 'yaml') {
        if (id && this._pendingCmds[id]) {
          const { resolve } = this._pendingCmds[id]
          delete this._pendingCmds[id]
          resolve(data)
        }
        return
      }

      // Full state (sent on connect and as heartbeat)
      if (type === 'state') {
        this.deviceName = data.device_name || ''
        this.uptimeMs = data.uptime_ms || 0
        this.scanning = data.scanning || false
        this.logCapture = data.log_capture || false
        this.dumpActive = data.dump_active || false
        if (data.freq) {
          this.freq.freq2 = data.freq.freq2 || this.freq.freq2
          this.freq.freq1 = data.freq.freq1 || this.freq.freq1
          this.freq.freq0 = data.freq.freq0 || this.freq.freq0
        }
        if (data.covers) {
          this.covers = data.covers.map(c => ({
            ...c,
            _edit: {
              open_duration_ms:  c.open_duration_ms,
              close_duration_ms: c.close_duration_ms,
              poll_interval_ms:  c.poll_interval_ms,
            }
          }))
        }
        if (data.discovered) {
          this.scanning = data.discovered.scanning
          this.allDiscovered = data.discovered.blinds || []
        }
        return
      }

      // Incremental covers update
      if (type === 'covers') {
        this.covers = data.map(c => ({
          ...c,
          _edit: {
            open_duration_ms:  c.open_duration_ms,
            close_duration_ms: c.close_duration_ms,
            poll_interval_ms:  c.poll_interval_ms,
          }
        }))
        return
      }

      // Incremental discovered update
      if (type === 'discovered') {
        this.scanning = data.scanning
        this.allDiscovered = data.blinds || []
        return
      }

      // New log entries
      if (type === 'log') {
        if (Array.isArray(data) && data.length > 0) {
          const newEntries = data.map((e, i) => ({ ...e, idx: this.logEntries.length + i }))
          this.logEntries.push(...newEntries)
          // Keep buffer to 500 entries
          if (this.logEntries.length > 500) this.logEntries.splice(0, this.logEntries.length - 500)
          if (this.logAutoScroll) {
            this.$nextTick(() => {
              const box = document.getElementById('log-box')
              if (box) box.scrollTop = box.scrollHeight
            })
          }
        }
        return
      }

      // Packets update
      if (type === 'packets') {
        this.dumpActive = data.dump_active
        this.dumpPackets = data.packets || []
        return
      }

      // Scan status update
      if (type === 'scan_status') {
        this.scanning = data.scanning
        return
      }
    },

    // ── Send command helper ───────────────────────────────────────────────────
    send(cmd, timeout = 5000) {
      return new Promise((resolve, reject) => {
        if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
          reject(new Error('Not connected'))
          return
        }

        const id = genId()
        cmd.id = id

        this._pendingCmds[id] = { resolve, reject }
        setTimeout(() => {
          if (this._pendingCmds[id]) {
            delete this._pendingCmds[id]
            reject(new Error('Timeout'))
          }
        }, timeout)

        this.ws.send(JSON.stringify(cmd))
      })
    },

    // ── Toast ─────────────────────────────────────────────────────────────────
    showToast(msg, isError = false) {
      if (this._toastTimer) clearTimeout(this._toastTimer)
      this.toast = { show: true, error: isError, msg }
      this._toastTimer = setTimeout(() => { this.toast.show = false }, 3500)
    },

    // ── Cover controls ────────────────────────────────────────────────────────
    toggleSettings(c) {
      this.settingsOpen = this.settingsOpen === c.blind_address ? null : c.blind_address
    },

    async coverCmd(c, action) {
      try {
        await this.send({ cmd: 'cover', address: c.blind_address, action })
        this.showToast(`${c.name}: ${action} sent`)
      } catch (e) {
        this.showToast(`Command failed: ${e.message}`, true)
      }
    },

    async saveSettings(c) {
      try {
        await this.send({
          cmd: 'settings',
          address: c.blind_address,
          open_duration:  c._edit.open_duration_ms,
          close_duration: c._edit.close_duration_ms,
          poll_interval:  c._edit.poll_interval_ms,
        })
        this.showToast(`${c.name}: settings saved`)
        this.settingsOpen = null
      } catch (e) {
        this.showToast(`Save failed: ${e.message}`, true)
      }
    },

    // ── Discovery ─────────────────────────────────────────────────────────────
    async startScan() {
      try {
        await this.send({ cmd: 'scan_start' })
        this.showToast('Scan started')
      } catch (e) { this.showToast(`Scan start failed: ${e.message}`, true) }
    },

    async stopScan() {
      try {
        await this.send({ cmd: 'scan_stop' })
        this.showToast('Scan stopped')
      } catch (e) { this.showToast(`Scan stop failed: ${e.message}`, true) }
    },

    startAdopt(b) {
      this.adoptTarget = b
      this.adoptName = ''
    },

    async confirmAdopt() {
      if (!this.adoptTarget) return
      try {
        await this.send({
          cmd: 'adopt',
          address: this.adoptTarget.blind_address,
          name: this.adoptName || this.adoptTarget.blind_address
        })
        this.showToast(`Adopted as "${this.adoptName || this.adoptTarget.blind_address}"`)
        this.adoptTarget = null
        this.tab = 'devices'
      } catch (e) { this.showToast(`Adopt failed: ${e.message}`, true) }
    },

    showYamlBlind(b) {
      this.yamlContent =
        `cover:\n` +
        `  - platform: elero\n` +
        `    blind_address: ${b.blind_address}\n` +
        `    channel: ${b.channel}\n` +
        `    remote_address: ${b.remote_address}\n` +
        `    name: "My Blind"\n` +
        `    # open_duration: 25s\n` +
        `    # close_duration: 22s\n` +
        `    hop: ${b.hop}\n` +
        `    payload_1: ${b.payload_1}\n` +
        `    payload_2: ${b.payload_2}\n` +
        `    pck_inf1: ${b.pck_inf1}\n` +
        `    pck_inf2: ${b.pck_inf2}\n`
    },

    async downloadYaml() {
      try {
        const yaml = await this.send({ cmd: 'get_yaml' }, 10000)
        const blob = new Blob([yaml], { type: 'text/plain' })
        const url  = URL.createObjectURL(blob)
        const a    = document.createElement('a')
        a.href = url; a.download = 'elero_blinds.yaml'; a.click()
        URL.revokeObjectURL(url)
      } catch (e) { this.showToast(`YAML download failed: ${e.message}`, true) }
    },

    copyYaml() {
      navigator.clipboard?.writeText(this.yamlContent)
        .then(() => this.showToast('Copied!'))
        .catch(() => this.showToast('Copy failed', true))
    },

    // ── Log ───────────────────────────────────────────────────────────────────
    async startCapture() {
      try {
        await this.send({ cmd: 'log_start' })
        this.logCapture = true
        this.showToast('Log capture started')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async stopCapture() {
      try {
        await this.send({ cmd: 'log_stop' })
        this.logCapture = false
        this.showToast('Log capture stopped')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async clearLog() {
      try {
        await this.send({ cmd: 'log_clear' })
        this.logEntries = []
        this.showToast('Log cleared')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    // Replace 0xABCDEF hex addresses with linked name annotations
    linkAddrs(msg) {
      if (!msg) return ''
      const addrMap = {}
      for (const c of this.covers) addrMap[c.blind_address] = c.name
      // Escape HTML first
      const safe = msg.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
      return safe.replace(/0x[0-9a-fA-F]{6}/g, m => {
        const name = addrMap[m.toLowerCase()] || addrMap[m]
        return name
          ? `${m}<span class="blind-ref">(${name})</span>`
          : m
      })
    },

    // ── Frequency ─────────────────────────────────────────────────────────────
    applyPreset(v) {
      if (!v) return
      const [f2, f1, f0] = v.split(',')
      this.freq = { freq2: '0x'+f2, freq1: '0x'+f1, freq0: '0x'+f0 }
    },

    async setFrequency() {
      this.freqStatus = 'Applying…'
      try {
        await this.send({
          cmd: 'set_frequency',
          freq2: this.freq.freq2,
          freq1: this.freq.freq1,
          freq0: this.freq.freq0
        })
        this.freqStatus = ''
        this.showToast(`Frequency set: ${this.freq.freq2} ${this.freq.freq1} ${this.freq.freq0}`)
      } catch (e) { this.freqStatus = ''; this.showToast(`Failed: ${e.message}`, true) }
    },

    // ── Packet dump ───────────────────────────────────────────────────────────
    async startDump() {
      try {
        await this.send({ cmd: 'dump_start' })
        this.dumpActive = true
        this.showToast('Packet dump started')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async stopDump() {
      try {
        await this.send({ cmd: 'dump_stop' })
        this.dumpActive = false
        this.showToast('Packet dump stopped')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async clearDump() {
      try {
        await this.send({ cmd: 'dump_clear' })
        this.dumpPackets = []
        this.showToast('Dump cleared')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    // ── Helpers exposed to template ───────────────────────────────────────────
    stateLabel: s => STATE_LABELS[s] || s || 'Unknown',
    relTime,
    fmtTs,
    rssiIcon,
  }))
})

Alpine.start()
