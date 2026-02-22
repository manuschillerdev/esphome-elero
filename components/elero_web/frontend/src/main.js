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

// ─── API helpers ─────────────────────────────────────────────────────────────
async function api(method, url, params = {}) {
  const qs = Object.keys(params).length
    ? '?' + new URLSearchParams(params).toString()
    : ''
  const r = await fetch(url + qs, { method })
  if (!r.ok) {
    let msg = `HTTP ${r.status}`
    try { const d = await r.json(); msg = d.error || msg } catch {}
    throw new Error(msg)
  }
  const ct = r.headers.get('content-type') || ''
  return ct.includes('application/json') ? r.json() : r.text()
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

// ─── Alpine app ──────────────────────────────────────────────────────────────
document.addEventListener('alpine:init', () => {
  Alpine.data('app', () => ({
    // Navigation
    tab: 'devices',

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
    logLastTs: 0,
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

    // Polling intervals
    _pollCovers: null,
    _pollDisc: null,
    _pollLog: null,
    _pollDump: null,

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    async init() {
      await this.refreshInfo()
      await this.refreshCovers()
      await this.loadFrequency()
      this._pollCovers = setInterval(() => this.refreshCovers(), 3000)
      this._pollDisc   = setInterval(() => this.refreshDiscovered(), 3000)
      this._pollDump   = setInterval(() => this.refreshDump(), 2000)
      this._pollLog    = setInterval(() => this.refreshLog(), 1500)
    },

    // ── Toast ─────────────────────────────────────────────────────────────────
    showToast(msg, isError = false) {
      if (this._toastTimer) clearTimeout(this._toastTimer)
      this.toast = { show: true, error: isError, msg }
      this._toastTimer = setTimeout(() => { this.toast.show = false }, 3500)
    },

    // ── Info ──────────────────────────────────────────────────────────────────
    async refreshInfo() {
      try {
        const d = await api('GET', '/elero/api/info')
        this.deviceName = d.device_name || ''
        this.uptimeMs = d.uptime_ms || 0
        this.freq.freq2 = d.freq2 || this.freq.freq2
        this.freq.freq1 = d.freq1 || this.freq.freq1
        this.freq.freq0 = d.freq0 || this.freq.freq0
      } catch {}
    },

    // ── Covers ────────────────────────────────────────────────────────────────
    async refreshCovers() {
      try {
        const d = await api('GET', '/elero/api/configured')
        this.covers = d.covers.map(c => ({
          ...c,
          _edit: {
            open_duration_ms:  c.open_duration_ms,
            close_duration_ms: c.close_duration_ms,
            poll_interval_ms:  c.poll_interval_ms,
          }
        }))
        // Also pull uptime from info periodically
        this.uptimeMs += 3000  // rough increment
      } catch {}
    },

    toggleSettings(c) {
      this.settingsOpen = this.settingsOpen === c.blind_address ? null : c.blind_address
    },

    async coverCmd(c, cmd) {
      try {
        await api('POST', `/elero/api/covers/${c.blind_address}/command`, { cmd })
        this.showToast(`${c.name}: ${cmd} sent`)
      } catch (e) {
        this.showToast(`Command failed: ${e.message}`, true)
      }
    },

    async saveSettings(c) {
      try {
        await api('POST', `/elero/api/covers/${c.blind_address}/settings`, {
          open_duration:  c._edit.open_duration_ms,
          close_duration: c._edit.close_duration_ms,
          poll_interval:  c._edit.poll_interval_ms,
        })
        this.showToast(`${c.name}: settings saved`)
        this.settingsOpen = null
        await this.refreshCovers()
      } catch (e) {
        this.showToast(`Save failed: ${e.message}`, true)
      }
    },

    // ── Discovery ─────────────────────────────────────────────────────────────
    async refreshDiscovered() {
      try {
        const d = await api('GET', '/elero/api/discovered')
        this.scanning = d.scanning
        this.allDiscovered = d.blinds || []
      } catch {}
    },

    async startScan() {
      try {
        await api('POST', '/elero/api/scan/start')
        this.scanning = true
        this.showToast('Scan started')
      } catch (e) { this.showToast(`Scan start failed: ${e.message}`, true) }
    },

    async stopScan() {
      try {
        await api('POST', '/elero/api/scan/stop')
        this.scanning = false
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
        await api('POST', `/elero/api/discovered/${this.adoptTarget.blind_address}/adopt`,
                  { name: this.adoptName || this.adoptTarget.blind_address })
        this.showToast(`Adopted as "${this.adoptName || this.adoptTarget.blind_address}"`)
        this.adoptTarget = null
        this.tab = 'devices'
        await this.refreshCovers()
        await this.refreshDiscovered()
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
        const text = await api('GET', '/elero/api/yaml')
        const blob = new Blob([text], { type: 'text/plain' })
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
        await api('POST', '/elero/api/logs/capture/start')
        this.logCapture = true
        this.showToast('Log capture started')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async stopCapture() {
      try {
        await api('POST', '/elero/api/logs/capture/stop')
        this.logCapture = false
        this.showToast('Log capture stopped')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async clearLog() {
      try {
        await api('POST', '/elero/api/logs/clear')
        this.logEntries = []
        this.logLastTs = 0
        this.showToast('Log cleared')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async refreshLog() {
      try {
        const d = await api('GET', '/elero/api/logs', { since: this.logLastTs })
        this.logCapture = d.capture_active
        if (d.entries && d.entries.length > 0) {
          const newEntries = d.entries.map((e, i) => ({ ...e, idx: this.logEntries.length + i }))
          this.logEntries.push(...newEntries)
          // Keep buffer to 500 entries
          if (this.logEntries.length > 500) this.logEntries.splice(0, this.logEntries.length - 500)
          this.logLastTs = newEntries[newEntries.length - 1].t
          if (this.logAutoScroll) {
            this.$nextTick(() => {
              const box = document.getElementById('log-box')
              if (box) box.scrollTop = box.scrollHeight
            })
          }
        }
      } catch {}
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
    async loadFrequency() {
      try {
        const d = await api('GET', '/elero/api/frequency')
        this.freq = { freq2: d.freq2, freq1: d.freq1, freq0: d.freq0 }
      } catch {}
    },

    applyPreset(v) {
      if (!v) return
      const [f2, f1, f0] = v.split(',')
      this.freq = { freq2: '0x'+f2, freq1: '0x'+f1, freq0: '0x'+f0 }
    },

    async setFrequency() {
      this.freqStatus = 'Applying…'
      try {
        const d = await api('POST', '/elero/api/frequency/set', this.freq)
        this.freq = { freq2: d.freq2, freq1: d.freq1, freq0: d.freq0 }
        this.freqStatus = ''
        this.showToast(`Frequency set: ${d.freq2} ${d.freq1} ${d.freq0}`)
      } catch (e) { this.freqStatus = ''; this.showToast(`Failed: ${e.message}`, true) }
    },

    // ── Packet dump ───────────────────────────────────────────────────────────
    async startDump() {
      try {
        await api('POST', '/elero/api/dump/start')
        this.dumpActive = true
        this.showToast('Packet dump started')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async stopDump() {
      try {
        await api('POST', '/elero/api/dump/stop')
        this.dumpActive = false
        this.showToast('Packet dump stopped')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async clearDump() {
      try {
        await api('POST', '/elero/api/packets/clear')
        this.dumpPackets = []
        this.showToast('Dump cleared')
      } catch (e) { this.showToast(`Failed: ${e.message}`, true) }
    },

    async refreshDump() {
      try {
        const d = await api('GET', '/elero/api/packets')
        this.dumpActive  = d.dump_active
        this.dumpPackets = d.packets || []
      } catch {}
    },

    // ── Helpers exposed to template ───────────────────────────────────────────
    stateLabel: s => STATE_LABELS[s] || s || 'Unknown',
    relTime,
    fmtTs,
    rssiIcon,
  }))
})

Alpine.start()
