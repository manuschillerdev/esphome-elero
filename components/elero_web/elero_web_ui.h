#pragma once

#ifdef __AVR__
#include <pgmspace.h>
#elif !defined(PROGMEM)
#define PROGMEM
#endif

namespace esphome {
namespace elero {

const char ELERO_WEB_UI_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Elero Blind Manager</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#f5f5f5;color:#333;line-height:1.5}
.container{max-width:720px;margin:0 auto;padding:16px}
header{background:#1a73e8;color:#fff;padding:16px;margin-bottom:16px;border-radius:8px;display:flex;align-items:center;gap:12px}
header h1{font-size:1.2em;font-weight:600}
.card{background:#fff;border-radius:8px;box-shadow:0 1px 3px rgba(0,0,0,.12);margin-bottom:16px;overflow:hidden}
.card-header{padding:12px 16px;background:#f8f9fa;border-bottom:1px solid #e0e0e0;display:flex;justify-content:space-between;align-items:center;font-weight:600;font-size:.95em}
.card-body{padding:16px}
.badge{display:inline-block;padding:2px 8px;border-radius:12px;font-size:.75em;font-weight:600}
.badge-scan{background:#e8f5e9;color:#2e7d32}
.badge-idle{background:#f5f5f5;color:#757575}
.badge-count{background:#e3f2fd;color:#1565c0}
.btn{display:inline-flex;align-items:center;gap:6px;padding:8px 16px;border:none;border-radius:6px;font-size:.9em;cursor:pointer;font-weight:500;transition:background .2s}
.btn-primary{background:#1a73e8;color:#fff}
.btn-primary:hover{background:#1557b0}
.btn-danger{background:#d32f2f;color:#fff}
.btn-danger:hover{background:#b71c1c}
.btn-outline{background:#fff;color:#1a73e8;border:1px solid #1a73e8}
.btn-outline:hover{background:#e3f2fd}
.btn:disabled{opacity:.5;cursor:not-allowed}
.device{border:1px solid #e0e0e0;border-radius:8px;padding:12px;margin-bottom:8px;transition:border-color .2s}
.device:hover{border-color:#1a73e8}
.device-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:4px}
.device-addr{font-family:monospace;font-weight:600;font-size:.95em}
.device-details{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:4px;font-size:.85em;color:#666}
.device-details span{display:flex;gap:4px}
.device-details .label{color:#999}
.state{display:inline-block;padding:1px 6px;border-radius:4px;font-size:.8em;font-weight:500}
.state-top{background:#e8f5e9;color:#2e7d32}
.state-bottom{background:#fce4ec;color:#c62828}
.state-moving_up,.state-start_moving_up,.state-opening{background:#fff3e0;color:#e65100}
.state-moving_down,.state-start_moving_down,.state-closing{background:#fff3e0;color:#e65100}
.state-idle{background:#f5f5f5;color:#757575}
.state-intermediate,.state-stopped{background:#e3f2fd;color:#1565c0}
.state-blocking,.state-overheated,.state-timeout{background:#ffebee;color:#c62828}
.configured-tag{font-size:.75em;color:#2e7d32;font-weight:600}
.empty{text-align:center;padding:24px;color:#999;font-size:.9em}
.modal-overlay{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.5);z-index:100;align-items:center;justify-content:center}
.modal-overlay.active{display:flex}
.modal{background:#fff;border-radius:12px;width:90%;max-width:560px;max-height:90vh;overflow-y:auto;box-shadow:0 8px 32px rgba(0,0,0,.2)}
.modal-header{padding:16px;border-bottom:1px solid #e0e0e0;display:flex;justify-content:space-between;align-items:center}
.modal-header h3{font-size:1em}
.modal-close{background:none;border:none;font-size:1.4em;cursor:pointer;color:#999;padding:0 4px}
.modal-close:hover{color:#333}
.modal-body{padding:16px}
.modal-footer{padding:12px 16px;border-top:1px solid #e0e0e0;display:flex;justify-content:flex-end;gap:8px}
.yaml-box{background:#263238;color:#e0e0e0;border-radius:6px;padding:12px;font-family:monospace;font-size:.82em;white-space:pre;overflow-x:auto;line-height:1.6;max-height:400px;overflow-y:auto}
.status-dot{width:8px;height:8px;border-radius:50%;display:inline-block}
.status-dot.scanning{background:#4caf50;animation:pulse 1s infinite}
.status-dot.idle{background:#9e9e9e}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.scan-status{display:flex;align-items:center;gap:8px;margin-bottom:12px}
.cover-pos{width:100%;height:4px;background:#e0e0e0;border-radius:2px;overflow:hidden;margin-top:4px}
.cover-pos-bar{height:100%;background:#1a73e8;transition:width .3s}
.refresh-hint{font-size:.8em;color:#999;margin-top:8px}
.toast{position:fixed;bottom:24px;left:50%;transform:translateX(-50%);background:#333;color:#fff;padding:10px 20px;border-radius:8px;font-size:.9em;z-index:200;opacity:0;transition:opacity .3s}
.toast.show{opacity:1}
.toast.error{background:#d32f2f}
.badge-dump{background:#fff3e0;color:#e65100}
.badge-dumping{background:#e8f5e9;color:#2e7d32}
.pkt-table{width:100%;border-collapse:collapse;font-size:.8em}
.pkt-table th{background:#f8f9fa;padding:5px 8px;text-align:left;border-bottom:2px solid #e0e0e0;font-weight:600;white-space:nowrap}
.pkt-table td{padding:4px 8px;border-bottom:1px solid #f0f0f0;vertical-align:top}
.pkt-table tr.pkt-ok{background:#f1f8f4}
.pkt-table tr.pkt-err{background:#fff5f5}
.pkt-hex{font-family:monospace;font-size:.85em;word-break:break-all;color:#333}
.pkt-ok-badge{color:#2e7d32;font-weight:600}
.pkt-err-badge{color:#c62828;font-weight:600}
.dump-wrap{overflow-x:auto;max-height:380px;overflow-y:auto;margin-top:12px}
</style>
</head>
<body>
<div class="container">
  <header>
    <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="18" height="18" rx="2"/><line x1="3" y1="9" x2="21" y2="9"/><line x1="3" y1="15" x2="21" y2="15"/></svg>
    <h1>Elero Blind Manager</h1>
  </header>

  <div class="card">
    <div class="card-header">
      <span>RF-Scan</span>
      <span id="scan-badge" class="badge badge-idle">Inaktiv</span>
    </div>
    <div class="card-body">
      <div class="scan-status">
        <span id="scan-dot" class="status-dot idle"></span>
        <span id="scan-text">Bereit zum Scannen</span>
      </div>
      <div style="display:flex;gap:8px">
        <button id="btn-start" class="btn btn-primary" onclick="startScan()">Scan starten</button>
        <button id="btn-stop" class="btn btn-danger" onclick="stopScan()" disabled>Scan stoppen</button>
      </div>
      <p class="refresh-hint">Betaetigen Sie waehrend des Scans Ihre Elero-Fernbedienung.</p>
    </div>
  </div>

  <div class="card">
    <div class="card-header">
      <span>Gefundene Geraete</span>
      <span id="disc-count" class="badge badge-count">0</span>
    </div>
    <div class="card-body" id="discovered-list">
      <div class="empty">Noch keine Geraete gefunden.</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">
      <span>Konfigurierte Covers</span>
      <span id="conf-count" class="badge badge-count">0</span>
    </div>
    <div class="card-body" id="configured-list">
      <div class="empty">Keine Covers konfiguriert.</div>
    </div>
  </div>

  <div class="card">
    <div class="card-header">
      <span>Packet-Dump</span>
      <div style="display:flex;gap:8px;align-items:center">
        <span id="dump-badge" class="badge badge-dump">Inaktiv</span>
        <span id="dump-count" class="badge badge-count">0</span>
      </div>
    </div>
    <div class="card-body">
      <div style="display:flex;gap:8px;flex-wrap:wrap">
        <button id="btn-dump-start" class="btn btn-primary" onclick="startDump()">Dump starten</button>
        <button id="btn-dump-stop" class="btn btn-danger" onclick="stopDump()" disabled>Stoppen</button>
        <button id="btn-dump-clear" class="btn btn-outline" onclick="clearDump()">Leeren</button>
      </div>
      <p class="refresh-hint">Zeichnet alle empfangenen HF-Pakete auf (max. 50). Gueltige Pakete = gruen, abgelehnte = rot.</p>
      <div id="dump-list" class="dump-wrap">
        <div class="empty">Kein Dump aktiv.</div>
      </div>
    </div>
  </div>

  <div style="text-align:center;margin-top:8px">
    <button class="btn btn-outline" onclick="showYaml()">YAML exportieren</button>
  </div>
</div>

<div id="yaml-modal" class="modal-overlay">
  <div class="modal">
    <div class="modal-header">
      <h3>YAML-Export</h3>
      <button class="modal-close" onclick="closeYaml()">&times;</button>
    </div>
    <div class="modal-body">
      <p style="margin-bottom:12px;font-size:.9em;color:#666">Fuegen Sie folgendes in Ihre ESPHome-Konfiguration ein:</p>
      <div id="yaml-content" class="yaml-box"></div>
    </div>
    <div class="modal-footer">
      <button class="btn btn-outline" onclick="copyYaml()">In Zwischenablage kopieren</button>
      <button class="btn btn-primary" onclick="closeYaml()">Schliessen</button>
    </div>
  </div>
</div>

<div id="toast" class="toast"></div>

<script>
var refreshTimer=null;
var scanning=false;
var toastTimer=null;

function showToast(msg,isError){
  var t=document.getElementById('toast');
  t.textContent=msg;
  t.className=isError?'toast error show':'toast show';
  if(toastTimer) clearTimeout(toastTimer);
  toastTimer=setTimeout(function(){t.className='toast';},4000);
}

function handleResponse(r){
  if(r.ok) return r.json();
  return r.json().then(function(d){
    throw new Error(d.error||('HTTP '+r.status));
  },function(){
    throw new Error('HTTP '+r.status);
  });
}

function startScan(){
  fetch('/elero/api/scan/start',{method:'POST'})
    .then(handleResponse)
    .then(function(){
      scanning=true;
      updateScanUI();
      startRefresh();
    })
    .catch(function(e){
      showToast('Scan-Start fehlgeschlagen: '+e.message,true);
      refresh();
    });
}

function stopScan(){
  fetch('/elero/api/scan/stop',{method:'POST'})
    .then(handleResponse)
    .then(function(){
      scanning=false;
      updateScanUI();
    })
    .catch(function(e){
      showToast('Scan-Stopp fehlgeschlagen: '+e.message,true);
      refresh();
    });
}

function updateScanUI(){
  var dot=document.getElementById('scan-dot');
  var badge=document.getElementById('scan-badge');
  var text=document.getElementById('scan-text');
  var btnStart=document.getElementById('btn-start');
  var btnStop=document.getElementById('btn-stop');
  if(scanning){
    dot.className='status-dot scanning';
    badge.className='badge badge-scan';
    badge.textContent='Scanne...';
    text.textContent='RF-Scan laeuft...';
    btnStart.disabled=true;
    btnStop.disabled=false;
  }else{
    dot.className='status-dot idle';
    badge.className='badge badge-idle';
    badge.textContent='Inaktiv';
    text.textContent='Scan beendet';
    btnStart.disabled=false;
    btnStop.disabled=true;
  }
}

function startRefresh(){
  if(refreshTimer) clearInterval(refreshTimer);
  refreshTimer=setInterval(refresh,3000);
  refresh();
}

function stateClass(s){
  if(!s||s==='unknown') return 'state-idle';
  return 'state-'+s;
}

function refresh(){
  fetch('/elero/api/discovered').then(function(r){
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.json();
  }).then(function(d){
    scanning=d.scanning;
    updateScanUI();
    var el=document.getElementById('discovered-list');
    document.getElementById('disc-count').textContent=d.blinds.length;
    if(!d.blinds.length){
      el.innerHTML='<div class="empty">Noch keine Geraete gefunden.</div>';
      return;
    }
    var html='';
    for(var i=0;i<d.blinds.length;i++){
      var b=d.blinds[i];
      html+='<div class="device"><div class="device-header">';
      html+='<span class="device-addr">'+b.blind_address+'</span>';
      if(b.already_configured){
        html+='<span class="configured-tag">Konfiguriert</span>';
      }
      html+='</div><div class="device-details">';
      html+='<span><span class="label">CH:</span> '+b.channel+'</span>';
      html+='<span><span class="label">Remote:</span> '+b.remote_address+'</span>';
      html+='<span><span class="label">RSSI:</span> '+b.rssi.toFixed(1)+' dBm</span>';
      html+='<span><span class="label">Gesehen:</span> '+b.times_seen+'x</span>';
      html+='<span><span class="label">State:</span> <span class="state '+stateClass(b.last_state)+'">'+b.last_state+'</span></span>';
      html+='<span><span class="label">Hop:</span> '+b.hop+'</span>';
      html+='</div></div>';
    }
    el.innerHTML=html;
  }).catch(function(e){
    console.error('Refresh discovered failed:',e);
  });

  fetch('/elero/api/configured').then(function(r){
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.json();
  }).then(function(d){
    var el=document.getElementById('configured-list');
    document.getElementById('conf-count').textContent=d.covers.length;
    if(!d.covers.length){
      el.innerHTML='<div class="empty">Keine Covers konfiguriert.</div>';
      return;
    }
    var html='';
    for(var i=0;i<d.covers.length;i++){
      var c=d.covers[i];
      html+='<div class="device"><div class="device-header">';
      html+='<span class="device-addr">'+c.name+'</span>';
      html+='<span class="state '+stateClass(c.operation)+'">'+c.operation+'</span>';
      html+='</div><div class="device-details">';
      html+='<span><span class="label">Adresse:</span> '+c.blind_address+'</span>';
      html+='<span><span class="label">Position:</span> '+(c.position*100).toFixed(0)+'%</span>';
      html+='</div>';
      html+='<div class="cover-pos"><div class="cover-pos-bar" style="width:'+(c.position*100)+'%"></div></div>';
      html+='</div>';
    }
    el.innerHTML=html;
  }).catch(function(e){
    console.error('Refresh configured failed:',e);
  });
}

function showYaml(){
  fetch('/elero/api/yaml').then(function(r){
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.text();
  }).then(function(t){
    document.getElementById('yaml-content').textContent=t;
    document.getElementById('yaml-modal').classList.add('active');
  }).catch(function(e){
    showToast('YAML-Export fehlgeschlagen: '+e.message,true);
  });
}

function closeYaml(){
  document.getElementById('yaml-modal').classList.remove('active');
}

function copyYaml(){
  var text=document.getElementById('yaml-content').textContent;
  navigator.clipboard.writeText(text).then(function(){
    var btn=document.querySelector('.modal-footer .btn-outline');
    btn.textContent='Kopiert!';
    setTimeout(function(){btn.textContent='In Zwischenablage kopieren';},2000);
  });
}

var dumpRefreshTimer=null;
var dumping=false;

function startDump(){
  fetch('/elero/api/dump/start',{method:'POST'})
    .then(handleResponse)
    .then(function(){
      dumping=true;
      updateDumpUI();
      if(!dumpRefreshTimer) dumpRefreshTimer=setInterval(refreshDump,2000);
      refreshDump();
    })
    .catch(function(e){showToast('Dump-Start fehlgeschlagen: '+e.message,true);});
}

function stopDump(){
  fetch('/elero/api/dump/stop',{method:'POST'})
    .then(handleResponse)
    .then(function(){
      dumping=false;
      updateDumpUI();
      if(dumpRefreshTimer){clearInterval(dumpRefreshTimer);dumpRefreshTimer=null;}
    })
    .catch(function(e){showToast('Dump-Stopp fehlgeschlagen: '+e.message,true);refreshDump();});
}

function clearDump(){
  fetch('/elero/api/packets/clear',{method:'POST'})
    .then(handleResponse)
    .then(function(){
      document.getElementById('dump-list').innerHTML='<div class="empty">Geleert.</div>';
      document.getElementById('dump-count').textContent='0';
    })
    .catch(function(e){showToast('Leeren fehlgeschlagen: '+e.message,true);});
}

function updateDumpUI(){
  var badge=document.getElementById('dump-badge');
  var btnS=document.getElementById('btn-dump-start');
  var btnX=document.getElementById('btn-dump-stop');
  if(dumping){
    badge.className='badge badge-dumping';badge.textContent='Aktiv';
    btnS.disabled=true;btnX.disabled=false;
  }else{
    badge.className='badge badge-dump';badge.textContent='Inaktiv';
    btnS.disabled=false;btnX.disabled=true;
  }
}

function refreshDump(){
  fetch('/elero/api/packets').then(function(r){
    if(!r.ok) throw new Error('HTTP '+r.status);
    return r.json();
  }).then(function(d){
    dumping=d.dump_active;
    updateDumpUI();
    if(dumping&&!dumpRefreshTimer) dumpRefreshTimer=setInterval(refreshDump,2000);
    if(!dumping&&dumpRefreshTimer){clearInterval(dumpRefreshTimer);dumpRefreshTimer=null;}
    document.getElementById('dump-count').textContent=d.count;
    var el=document.getElementById('dump-list');
    if(!d.count){
      el.innerHTML='<div class="empty">Keine Pakete gespeichert.</div>';
      return;
    }
    var pkts=d.packets.slice().sort(function(a,b){return b.t-a.t;});
    var html='<table class="pkt-table"><thead><tr><th>Zeit(ms)</th><th>Len</th><th>Status</th><th>Ursache</th><th>Hex</th></tr></thead><tbody>';
    for(var i=0;i<pkts.length;i++){
      var p=pkts[i];
      var rc=p.valid?'pkt-ok':'pkt-err';
      var sb=p.valid?'<span class="pkt-ok-badge">OK</span>':'<span class="pkt-err-badge">ERR</span>';
      html+='<tr class="'+rc+'"><td>'+p.t+'</td><td>'+p.len+'</td><td>'+sb+'</td><td>'+(p.reason||'')+'</td><td class="pkt-hex">'+p.hex+'</td></tr>';
    }
    html+='</tbody></table>';
    el.innerHTML=html;
  }).catch(function(e){console.error('Refresh dump failed:',e);});
}

// Initial load
startRefresh();
refreshDump();
</script>
</body>
</html>)rawliteral";

}  // namespace elero
}  // namespace esphome
