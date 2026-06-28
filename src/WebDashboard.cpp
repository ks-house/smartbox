#include "WebDashboard.h"
#include "ConfigManager.h"
#include "WifiManager.h"
#include "AutoOtaManager.h"
#include "RemoteLogger.h"
#include "secrets.h"
#include <Arduino.h>
#include <WiFi.h>

AsyncWebServer WebDashboard::server(80);
AsyncWebSocket WebDashboard::wsLog("/api/logs");
SmartBoxController* WebDashboard::controllerPtr = nullptr;

// JSON string escape helper (anonymous namespace — internal linkage only)
namespace {
    String escapeJson(const String& input) {
        String out;
        out.reserve(input.length() + 8);
        for (unsigned int i = 0; i < input.length(); i++) {
            char c = input.charAt(i);
            if      (c == '"')  { out += '\\'; out += '"'; }
            else if (c == '\\') { out += '\\'; out += '\\'; }
            else if (c == '\n') { out += '\\'; out += 'n'; }
            else if (c == '\r') { out += '\\'; out += 'r'; }
            else if (c == '\t') { out += '\\'; out += 't'; }
            else                { out += c; }
        }
        return out;
    }
}

// Embedded Premium Responsive HTML/CSS/JS Dashboard
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <meta name='viewport' content='width=device-width, initial-scale=1'>
  <title>SmartBox Control Panel</title>
  <link href='https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;600;800&display=swap' rel='stylesheet'>
  <style>
    :root {
      --bg-color: #0b0c10;
      --card-bg: rgba(255, 255, 255, 0.03);
      --card-border: rgba(255, 255, 255, 0.06);
      --text-color: #f1f3f9;
      --text-muted: #858a9d;
      --primary: #5856d6;
      --success: #10b981;
      --danger: #ef4444;
      --warning: #f59e0b;
      --info: #0a84ff;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: 'Outfit', sans-serif; }
    body { background-color: var(--bg-color); color: var(--text-color); padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
    .container { width: 100%; max-width: 460px; }
    .header { text-align: center; margin-bottom: 25px; }
    .header h1 { font-size: 2.2rem; font-weight: 800; background: linear-gradient(135deg, #a5b4fc, #5856d6); -webkit-background-clip: text; -webkit-text-fill-color: transparent; }
    .header p { color: var(--text-muted); font-size: 0.95rem; margin-top: 5px; }
    .card { background: var(--card-bg); border: 1px solid var(--card-border); border-radius: 24px; padding: 25px; backdrop-filter: blur(20px); -webkit-backdrop-filter: blur(20px); box-shadow: 0 30px 60px rgba(0,0,0,0.4); margin-bottom: 20px; position: relative; overflow: hidden; transition: all 0.3s; }
    .card::before { content: ''; position: absolute; top: 0; left: 0; width: 100%; height: 4px; background: var(--primary); }
    .status-card.state-idle::before { background: var(--success); }
    .status-card.state-running::before { background: var(--info); }
    .status-card.state-emergency::before { background: var(--danger); }
    .status-card.state-battery::before { background: var(--warning); }
    .status-badge { display: inline-flex; align-items: center; padding: 6px 14px; border-radius: 30px; font-weight: 600; font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.5px; }
    .status-badge.idle { background: rgba(16, 185, 129, 0.15); color: var(--success); }
    .status-badge.running { background: rgba(10, 132, 255, 0.15); color: var(--info); }
    .status-badge.emergency { background: rgba(239, 68, 68, 0.15); color: var(--danger); animation: pulse 1.5s infinite; }
    .status-badge.battery { background: rgba(245, 158, 11, 0.15); color: var(--warning); }
    @keyframes pulse { 0% { opacity: 0.6; } 50% { opacity: 1; } 100% { opacity: 0.6; } }
    .state-title { font-size: 1.6rem; font-weight: 600; margin: 15px 0 5px 0; }
    .state-desc { color: var(--text-muted); font-size: 0.9rem; margin-bottom: 20px; }
    .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
    .metric-box { background: rgba(255, 255, 255, 0.01); border: 1px solid var(--card-border); border-radius: 16px; padding: 15px; text-align: center; }
    .metric-label { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; margin-bottom: 5px; }
    .metric-value { font-size: 1.35rem; font-weight: 600; }
    .btn { display: block; width: 100%; border: none; padding: 16px; border-radius: 16px; font-weight: 600; font-size: 0.95rem; cursor: pointer; transition: all 0.2s; text-align: center; margin-bottom: 10px; }
    .btn-danger { background: var(--danger); color: white; box-shadow: 0 8px 20px rgba(239, 68, 68, 0.2); }
    .btn-danger:hover { background: #dc2626; transform: translateY(-2px); }
    .btn-primary { background: var(--primary); color: white; box-shadow: 0 8px 20px rgba(88, 86, 214, 0.2); }
    .btn-primary:hover { background: #4745b8; transform: translateY(-2px); }
    .btn-success { background: var(--success); color: white; box-shadow: 0 8px 20px rgba(16, 185, 129, 0.2); }
    .btn-success:hover { background: #059669; transform: translateY(-2px); }
    .btn-disabled { background: rgba(255,255,255,0.04); color: var(--text-muted); cursor: not-allowed; }
    .form-group { margin-bottom: 15px; }
    .form-group label { display: block; font-size: 0.8rem; color: var(--text-muted); margin-bottom: 6px; text-transform: uppercase; letter-spacing: 0.5px; }
    .form-control { width: 100%; padding: 12px; border-radius: 12px; border: 1px solid var(--card-border); background: rgba(255, 255, 255, 0.02); color: var(--text-color); font-size: 1rem; transition: border 0.2s; }
    .form-control:focus { outline: none; border-color: var(--primary); }
  </style>
</head>
<body>
  <div class='container'>
    <div class='header'>
      <h1>SmartBox ☀️</h1>
      <p>Autonomous Solar Trash Collector | <span id='fwVersion'>v--</span> | Wi-Fi: <span id='wifiStatusBadge' style='font-weight:600; color:var(--danger);'>Offline</span></p>
    </div>
    
    <div class='card status-card' id='statusCard'>
      <div>
        <span class='status-badge' id='statusBadge'>Connecting...</span>
      </div>
      <h2 class='state-title' id='stateTitle'>Connecting...</h2>
      <p class='state-desc' id='stateDesc'>Please wait while connecting to the smart controller.</p>
      
      <div class='grid'>
        <div class='metric-box'>
          <div class='metric-label'>🔋 Battery Voltage</div>
          <div class='metric-value' id='valBattery'>-- V</div>
        </div>
        <div class='metric-box'>
          <div class='metric-label'>⚡ Actuator Current</div>
          <div class='metric-value' id='valCurrent'>-- mA</div>
        </div>
        <div class='metric-box'>
          <div class='metric-label'>📏 Human Distance</div>
          <div class='metric-value' id='valDistance'>-- cm</div>
        </div>
        <div class='metric-box'>
          <div class='metric-label'>⏱️ Active Time</div>
          <div class='metric-value' id='valTime'>-- s</div>
        </div>
      </div>
    </div>

    <!-- Remote Commands -->
    <button class='btn btn-disabled' id='btnRelease' disabled onclick='releaseEmergency()'>Release Emergency Lock</button>
    <div style='display:flex; gap:10px; margin-bottom:10px;'>
      <button class='btn btn-primary' id='btnOpen' onclick='forceOpen()' style='margin-bottom:0;'>Remote Force Open</button>
      <button class='btn btn-danger' id='btnClose' onclick='forceClose()' style='margin-bottom:0; background:var(--warning);'>Remote Force Close</button>
    </div>
    <button class='btn' id='btnMaintenance' onclick='toggleMaintenance()' style='background: var(--warning); box-shadow: 0 8px 20px rgba(245,158,11,0.2); color: white;'>🧹 수동 청소 모드 (5분 열림)</button>

    <!-- Sensitivity Settings -->
    <div class='card'>
      <h3 style='font-size: 1.15rem; font-weight: 600; margin-bottom: 18px; color: var(--primary);'>⚙️ Sensitivity Calibration</h3>
      <div class='form-group'>
        <label>📏 Detection Distance (cm)</label>
        <input type='number' id='cfgDistance' class='form-control' placeholder='50'>
      </div>
      <div class='form-group'>
        <label>⏱️ Lid Hold Time (ms)</label>
        <input type='number' id='cfgWait' class='form-control' placeholder='5000'>
      </div>
      <div class='form-group'>
        <label>⚡ Motor Stall Current (mA)</label>
        <input type='number' id='cfgStall' class='form-control' placeholder='800'>
      </div>
      <div class='form-group'>
        <label>⏰ Auto-OTA Hour (0-23)</label>
        <input type='number' id='cfgOtaHour' class='form-control' placeholder='3' min='0' max='23'>
      </div>
      <div class='form-group'>
        <label>⏰ Telemetry Interval (minutes)</label>
        <input type='number' id='cfgTelInterval' class='form-control' placeholder='10' min='1' max='1440'>
      </div>
      <button class='btn btn-success' onclick='updateConfig()'>Save Settings</button>
    </div>

    <!-- Network Configuration -->
    <div class='card'>
      <h3 style='font-size: 1.15rem; font-weight: 600; margin-bottom: 18px; color: var(--info);'>🌐 Network Configuration</h3>
      <div class='form-group'>
        <label>Select Wi-Fi Network</label>
        <select id='wifiSsid' class='form-control' style='margin-bottom: 10px;'>
          <option value=''>-- Click Scan to find networks --</option>
        </select>
        <button class='btn btn-primary' id='btnWifiScan' onclick='scanWifi()' style='background: var(--info); box-shadow: 0 8px 20px rgba(10,132,255,0.2);'>Scan Networks</button>
      </div>
      <div class='form-group'>
        <label>Password</label>
        <input type='password' id='wifiPassword' class='form-control' placeholder='Enter password'>
      </div>
      <div id='wifiStatus' style='display:none; padding: 12px; border-radius: 12px; margin-bottom: 15px; font-weight: 600; text-align: center;'></div>
      <button class='btn btn-success' id='btnWifiConnect' onclick='connectWifi()'>Connect</button>
    </div>

    <!-- NAS HTTPS OTA -->
    <div class='card'>
      <h3 style='font-size: 1.15rem; font-weight: 600; margin-bottom: 18px; color: var(--info);'>☁️ Firmware Update (from NAS)</h3>
      <div class='form-group'>
        <label>HTTPS Target URL</label>
        <input type='text' id='nasUrl' class='form-control' value='Loading...' readonly style='background: rgba(255,255,255,0.01); color: var(--text-muted);'>
      </div>
      <div id='nasOtaStatus' style='display:none; padding: 12px; border-radius: 12px; margin-bottom: 15px; font-weight: 600; text-align: center;'></div>
      <button class='btn btn-primary' id='btnNasOta' onclick='updateFromNas()' style='background: var(--info); box-shadow: 0 8px 20px rgba(10,132,255,0.2);'>Fetch & Update</button>
    </div>

    <!-- ===== Remote Log Viewer ===== -->
    <div class='card' style='margin-top: 20px;'>
      <div style='display:flex; justify-content:space-between; align-items:center; margin-bottom:14px;'>
        <h3 style='font-size:1.15rem; font-weight:600; color:#a5b4fc; margin:0;'>📋 Remote Serial Log</h3>
        <div style='display:flex; gap:8px; align-items:center;'>
          <span id='wsStatusDot' style='display:inline-block;width:10px;height:10px;border-radius:50%;background:var(--danger);'></span>
          <span id='wsStatusTxt' style='font-size:0.78rem; color:var(--text-muted);'>Disconnected</span>
          <button onclick='clearLogView()' style='padding:4px 10px;border-radius:8px;border:1px solid rgba(255,255,255,0.1);background:rgba(255,255,255,0.04);color:var(--text-muted);cursor:pointer;font-size:0.78rem;'>Clear</button>
          <button onclick='toggleLogPause()' id='btnLogPause' style='padding:4px 10px;border-radius:8px;border:1px solid rgba(255,255,255,0.1);background:rgba(255,255,255,0.04);color:var(--text-muted);cursor:pointer;font-size:0.78rem;'>Pause</button>
        </div>
      </div>
      <!-- Level filter chips -->
      <div style='display:flex; gap:6px; margin-bottom:10px; flex-wrap:wrap;'>
        <span onclick='toggleFilter(this,"DEBUG")' data-lvl='DEBUG' class='log-chip chip-on' style='cursor:pointer;padding:3px 10px;border-radius:20px;font-size:0.72rem;background:rgba(133,138,157,0.15);color:#858a9d;'>DEBUG</span>
        <span onclick='toggleFilter(this,"INFO")'  data-lvl='INFO'  class='log-chip chip-on' style='cursor:pointer;padding:3px 10px;border-radius:20px;font-size:0.72rem;background:rgba(16,185,129,0.15);color:#10b981;'>INFO</span>
        <span onclick='toggleFilter(this,"WARN")'  data-lvl='WARN'  class='log-chip chip-on' style='cursor:pointer;padding:3px 10px;border-radius:20px;font-size:0.72rem;background:rgba(245,158,11,0.15);color:#f59e0b;'>WARN</span>
        <span onclick='toggleFilter(this,"ERROR")' data-lvl='ERROR' class='log-chip chip-on' style='cursor:pointer;padding:3px 10px;border-radius:20px;font-size:0.72rem;background:rgba(239,68,68,0.15);color:#ef4444;'>ERROR</span>
      </div>
      <div id='logContainer' style='font-family:monospace;font-size:0.78rem;background:rgba(0,0,0,0.4);border:1px solid rgba(255,255,255,0.06);border-radius:12px;padding:12px;height:280px;overflow-y:auto;color:#c9d1e3;'></div>
    </div>
  </div>

  <script>
    let configLoaded = false;
    
    async function updateStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        
        const badge = document.getElementById('statusBadge');
        const title = document.getElementById('stateTitle');
        const desc = document.getElementById('stateDesc');
        const card = document.getElementById('statusCard');
        const btnRelease = document.getElementById('btnRelease');
        
        document.getElementById('valBattery').innerText = data.battery.toFixed(2) + ' V';
        document.getElementById('valCurrent').innerText = data.current.toFixed(0) + ' mA';
        document.getElementById('valDistance').innerText = data.distance.toFixed(0) + ' cm';
        document.getElementById('valTime').innerText = (data.time / 1000).toFixed(1) + ' s';
        document.getElementById('fwVersion').innerText = 'v' + data.version;
        
        const wifiBadge = document.getElementById('wifiStatusBadge');
        if (data.wifiConnected) {
          wifiBadge.innerText = 'Online';
          wifiBadge.style.color = 'var(--success)';
        } else {
          wifiBadge.innerText = 'Offline';
          wifiBadge.style.color = 'var(--danger)';
        }
        
        // Populate inputs once on load
        if (!configLoaded && data.config) {
          document.getElementById('cfgDistance').value = data.config.dist;
          document.getElementById('cfgWait').value = data.config.wait;
          document.getElementById('cfgStall').value = data.config.stall;
          document.getElementById('cfgOtaHour').value = data.config.otaHour;
          document.getElementById('cfgTelInterval').value = data.config.telInterval;
          document.getElementById('nasUrl').value = data.firmwareUrl || '';
          configLoaded = true;
        }
        
        card.className = 'card status-card';
        btnRelease.className = 'btn btn-disabled';
        btnRelease.disabled = true;
        
        const btnMaint = document.getElementById('btnMaintenance');
        if (data.maintenance) {
          btnMaint.innerText = `🧹 청소 모드 진행 중... (${data.maintenanceTime}초 남음)`;
          btnMaint.setAttribute('data-active', 'true');
          btnMaint.style.background = 'var(--danger)';
          btnMaint.style.boxShadow = '0 8px 20px rgba(239,68,68,0.2)';
        } else {
          btnMaint.innerText = '🧹 수동 청소 모드 (5분 열림)';
          btnMaint.setAttribute('data-active', 'false');
          btnMaint.style.background = 'var(--warning)';
          btnMaint.style.boxShadow = '0 8px 20px rgba(245,158,11,0.2)';
        }
        
        switch(data.state) {
          case 'IDLE':
            badge.className = 'status-badge idle';
            badge.innerText = 'Standby';
            title.innerText = 'Monitoring Ready';
            desc.innerText = 'System is ready and waiting for human approach.';
            card.classList.add('state-idle');
            break;
          case 'OPEN_START':
          case 'OPENING':
            badge.className = 'status-badge running';
            badge.innerText = 'Opening';
            title.innerText = 'Opening Lid';
            desc.innerText = 'Linear actuator is extending to push the lid open.';
            card.classList.add('state-running');
            break;
          case 'HOLD':
            badge.className = 'status-badge running';
            badge.innerText = 'Open';
            title.innerText = 'Lid Held Open';
            desc.innerText = 'Trash bin is open. Ready for waste deposition.';
            card.classList.add('state-running');
            break;
          case 'CLOSE_START':
          case 'CLOSING':
            badge.className = 'status-badge running';
            badge.innerText = 'Closing';
            title.innerText = 'Closing Lid';
            desc.innerText = 'Lid is lowering. Interlock and human detection active.';
            card.classList.add('state-running');
            break;
          case 'EMERGENCY_STOP':
            badge.className = 'status-badge emergency';
            badge.innerText = 'Emergency';
            title.innerText = 'System Locked';
            desc.innerText = 'Stall current detected or manual stop triggered!';
            card.classList.add('state-emergency');
            btnRelease.className = 'btn btn-danger';
            btnRelease.disabled = false;
            break;
          case 'BATTERY_LOW_SHUTDOWN':
            badge.className = 'status-badge battery';
            badge.innerText = 'Low Battery';
            title.innerText = 'Lid Locked Open';
            desc.innerText = 'Battery voltage dropped below 11.3V. Relays isolated.';
            card.classList.add('state-battery');
            break;
          case 'STARTUP_OPEN':
            badge.className = 'status-badge running';
            badge.innerText = 'Startup Open';
            title.innerText = 'Lid Open on Startup';
            desc.innerText = 'Lid is open. Waiting for human approach to start 10s countdown to close.';
            card.classList.add('state-running');
            break;
          case 'OTA_UPDATING':
            badge.className = 'status-badge battery';
            badge.innerText = '⏫ OTA Update';
            title.innerText = 'Firmware Updating';
            desc.innerText = 'OTA flash in progress. All relays safely shut down. Do not power off!';
            card.classList.add('state-battery');
            break;
          case 'MAINTENANCE':
            badge.className = 'status-badge battery';
            badge.innerText = '🧹 청소 모드';
            title.innerText = '수동 청소 모드 작동 중';
            desc.innerText = '작업을 위해 뚜껑이 열려 있습니다. 5분 후 자동 종료됩니다.';
            card.classList.add('state-battery');
            break;
        }
      } catch(e) {
        console.error(e);
      }
    }
    
    async function releaseEmergency() {
      if(confirm('Are you sure you want to release the emergency lock?')) {
        const btn = document.getElementById('btnRelease');
        btn.className = 'btn btn-disabled';
        btn.disabled = true;
        await fetch('/api/release');
        updateStatus();
      }
    }
    
    async function forceOpen() {
      if(confirm('Force open the lid remotely?')) {
        await fetch('/api/open');
        updateStatus();
      }
    }
    
    async function forceClose() {
      if(confirm('Force close the lid remotely?')) {
        await fetch('/api/close');
        updateStatus();
      }
    }
    
    async function toggleMaintenance() {
      const btn = document.getElementById('btnMaintenance');
      const active = btn.getAttribute('data-active') === 'true';
      const action = active ? 'stop' : 'start';
      if (active) {
        if (!confirm('청소 모드를 종료하고 뚜껑을 닫으시겠습니까?')) return;
      } else {
        if (!confirm('수동 청소 모드를 시작하겠습니까? 5분 동안 뚜껑이 열려 있습니다.')) return;
      }
      await fetch(`/api/maintenance?action=${action}`);
      updateStatus();
    }
    
    async function updateConfig() {
      const dist = document.getElementById('cfgDistance').value;
      const wait = document.getElementById('cfgWait').value;
      const stall = document.getElementById('cfgStall').value;
      const otaHour = document.getElementById('cfgOtaHour').value;
      const telInterval = document.getElementById('cfgTelInterval').value;
      
      try {
        const res = await fetch(`/api/config?dist=${dist}&wait=${wait}&stall=${stall}&otaHour=${otaHour}&telInterval=${telInterval}`);
        const result = await res.json();
        if (result.status === 'updated') {
          alert('Config parameters updated and saved!');
        } else {
          alert('Failed to update config: ' + result.message);
        }
        updateStatus();
      } catch(e) {
        alert('Server connection error.');
      }
    }

    async function updateFromNas() {
      if (!confirm('⚠️ Fetch firmware from NAS and reboot the device?\n\nAll relays will be safely shut down before flashing.')) return;
      
      const statusDiv = document.getElementById('nasOtaStatus');
      const btn = document.getElementById('btnNasOta');
      
      statusDiv.style.display = 'block';
      statusDiv.style.background = 'rgba(10,132,255,0.15)';
      statusDiv.style.color = 'var(--info)';
      statusDiv.innerText = '⏳ NAS 연결 및 버전 확인 중...';
      btn.disabled = true;
      btn.className = 'btn btn-disabled';
      
      try {
        const res = await fetch('/api/update-from-nas');
        const result = await res.json();
        if (result.status === 'started') {
          let checkInterval = setInterval(async () => {
            try {
              const checkRes = await fetch('/api/status');
              const checkData = await checkRes.json();
              
              if (checkData.otaState === 'CHECKING') {
                statusDiv.innerText = '⏳ NAS 연결 및 버전 확인 중...';
              } else if (checkData.otaState === 'UP_TO_DATE') {
                clearInterval(checkInterval);
                statusDiv.style.background = 'rgba(16,185,129,0.15)';
                statusDiv.style.color = 'var(--success)';
                statusDiv.innerText = '✅ 최신 버전입니다. 업데이트가 필요 없습니다.';
                btn.disabled = false;
                btn.className = 'btn btn-primary';
                btn.style.background = 'var(--info)';
              } else if (checkData.otaState === 'UPDATING') {
                statusDiv.innerText = '⏳ 펌웨어 다운로드 및 플래싱 중... 전원을 끄지 마세요.';
              } else if (checkData.otaState === 'FAILED') {
                clearInterval(checkInterval);
                statusDiv.style.background = 'rgba(239,68,68,0.15)';
                statusDiv.style.color = 'var(--danger)';
                statusDiv.innerText = '❌ 업데이트 실패: ' + (checkData.otaError || '알 수 없는 오류');
                btn.disabled = false;
                btn.className = 'btn btn-primary';
                btn.style.background = 'var(--info)';
              }
            } catch (e) {
              // Fetch failed, probably because of reboot!
              clearInterval(checkInterval);
              statusDiv.style.background = 'rgba(16,185,129,0.15)';
              statusDiv.style.color = 'var(--success)';
              statusDiv.innerText = '✅ 업데이트 완료! 장치 재부팅 중...';
              setTimeout(() => {
                location.reload();
              }, 5000);
            }
          }, 1500);
        } else {
          statusDiv.style.background = 'rgba(239,68,68,0.15)';
          statusDiv.style.color = 'var(--danger)';
          statusDiv.innerText = '❌ OTA 트리거 실패: ' + (result.message || 'Unknown error');
          btn.disabled = false;
          btn.className = 'btn btn-primary';
          btn.style.background = 'var(--info)';
        }
      } catch(e) {
        statusDiv.style.background = 'rgba(239,68,68,0.15)';
        statusDiv.style.color = 'var(--danger)';
        statusDiv.innerText = '❌ 서버 통신 오류';
        btn.disabled = false;
        btn.className = 'btn btn-primary';
        btn.style.background = 'var(--info)';
      }
    }

    let scanPollingInterval = null;

    async function scanWifi() {
      const btnScan = document.getElementById('btnWifiScan');
      const selectSsid = document.getElementById('wifiSsid');
      
      btnScan.disabled = true;
      btnScan.className = 'btn btn-disabled';
      btnScan.innerText = 'Scanning...';
      
      selectSsid.innerHTML = '<option value="">-- Scanning... --</option>';
      
      try {
        const response = await fetch('/api/wifi/scan?start=true');
        const data = await response.json();
        
        if (data.status === 'scanning') {
          if (scanPollingInterval) clearInterval(scanPollingInterval);
          
          scanPollingInterval = setInterval(async () => {
            try {
              const pollResponse = await fetch('/api/wifi/scan');
              const pollData = await pollResponse.json();
              
              if (pollData.status === 'complete') {
                clearInterval(scanPollingInterval);
                scanPollingInterval = null;
                
                btnScan.disabled = false;
                btnScan.className = 'btn btn-primary';
                btnScan.style.background = 'var(--info)';
                btnScan.innerText = 'Scan Networks';
                
                selectSsid.innerHTML = '';
                if (pollData.networks && pollData.networks.length > 0) {
                  pollData.networks.forEach(net => {
                    const opt = document.createElement('option');
                    opt.value = net.ssid;
                    opt.innerText = net.ssid + ' (' + net.rssi + ' dBm)';
                    selectSsid.appendChild(opt);
                  });
                } else {
                  selectSsid.innerHTML = '<option value="">No networks found</option>';
                }
              } else if (pollData.status === 'failed') {
                clearInterval(scanPollingInterval);
                scanPollingInterval = null;
                
                btnScan.disabled = false;
                btnScan.className = 'btn btn-primary';
                btnScan.style.background = 'var(--info)';
                btnScan.innerText = 'Scan Networks';
                selectSsid.innerHTML = '<option value="">Scan failed. Try again.</option>';
              }
            } catch (e) {
              console.error('Error polling wifi scan:', e);
            }
          }, 1000);
        }
      } catch (e) {
        console.error('Error starting wifi scan:', e);
        btnScan.disabled = false;
        btnScan.className = 'btn btn-primary';
        btnScan.style.background = 'var(--info)';
        btnScan.innerText = 'Scan Networks';
        selectSsid.innerHTML = '<option value="">Error starting scan</option>';
      }
    }

    async function connectWifi() {
      const selectSsid = document.getElementById('wifiSsid');
      const inputPass = document.getElementById('wifiPassword');
      const statusDiv = document.getElementById('wifiStatus');
      const btnConnect = document.getElementById('btnWifiConnect');
      
      const ssid = selectSsid.value;
      const password = inputPass.value;
      
      if (!ssid) {
        alert('Please select or scan a Wi-Fi network first.');
        return;
      }
      
      statusDiv.style.display = 'block';
      statusDiv.style.background = 'rgba(10,132,255,0.15)';
      statusDiv.style.color = 'var(--info)';
      statusDiv.innerText = '⏳ 연결 시도 중...';
      
      btnConnect.disabled = true;
      btnConnect.className = 'btn btn-disabled';
      
      try {
        const response = await fetch('/api/wifi/connect?ssid=' + encodeURIComponent(ssid) + '&password=' + encodeURIComponent(password));
        const data = await response.json();
        
        if (data.status === 'connecting') {
          setTimeout(() => {
            btnConnect.disabled = false;
            btnConnect.className = 'btn btn-success';
            statusDiv.style.display = 'none';
          }, 5000);
        } else {
          statusDiv.style.background = 'rgba(239,68,68,0.15)';
          statusDiv.style.color = 'var(--danger)';
          statusDiv.innerText = '❌ 연결 실패: ' + (data.message || 'Unknown error');
          btnConnect.disabled = false;
          btnConnect.className = 'btn btn-success';
        }
      } catch (e) {
        statusDiv.style.background = 'rgba(239,68,68,0.15)';
        statusDiv.style.color = 'var(--danger)';
        statusDiv.innerText = '❌ 서버와 통신 실패.';
        btnConnect.disabled = false;
        btnConnect.className = 'btn btn-success';
      }
    }
    
    setInterval(updateStatus, 1000);
    updateStatus();

    // ===== WebSocket Remote Logger =====
    const LOG_COLORS = { DEBUG:'#858a9d', INFO:'#10b981', WARN:'#f59e0b', ERROR:'#ef4444' };
    let logPaused = false;
    let activeFilters = new Set(['DEBUG','INFO','WARN','ERROR']);
    let logBuffer = [];

    function toggleFilter(el, lvl) {
      if (activeFilters.has(lvl)) {
        activeFilters.delete(lvl);
        el.style.opacity = '0.35';
        el.classList.remove('chip-on');
      } else {
        activeFilters.add(lvl);
        el.style.opacity = '1';
        el.classList.add('chip-on');
      }
      rebuildLogView();
    }

    function rebuildLogView() {
      const container = document.getElementById('logContainer');
      container.innerHTML = '';
      logBuffer.forEach(e => { if (activeFilters.has(e.lvl)) appendLogLine(e, false); });
      container.scrollTop = container.scrollHeight;
    }

    function appendLogLine(entry, scroll=true) {
      if (!activeFilters.has(entry.lvl)) return;
      const container = document.getElementById('logContainer');
      const div = document.createElement('div');
      div.style.cssText = 'padding:2px 0; border-bottom:1px solid rgba(255,255,255,0.03); white-space:pre-wrap; word-break:break-all;';
      const uptimeSec = (entry.ts / 1000).toFixed(1);
      const col = LOG_COLORS[entry.lvl] || '#c9d1e3';
      div.innerHTML = `<span style='color:#4b5563;'>[+${uptimeSec}s]</span> <span style='color:${col};font-weight:600;'>${entry.lvl.padEnd(5)}</span> <span>${entry.msg}</span>`;
      container.appendChild(div);
      if (scroll && !logPaused) container.scrollTop = container.scrollHeight;
    }

    function clearLogView() {
      logBuffer = [];
      document.getElementById('logContainer').innerHTML = '';
    }

    function toggleLogPause() {
      logPaused = !logPaused;
      document.getElementById('btnLogPause').innerText = logPaused ? 'Resume' : 'Pause';
      if (!logPaused) document.getElementById('logContainer').scrollTop = document.getElementById('logContainer').scrollHeight;
    }

    function connectWS() {
      const dot = document.getElementById('wsStatusDot');
      const txt = document.getElementById('wsStatusTxt');
      const wsUrl = 'ws://' + location.host + '/api/logs';
      const ws = new WebSocket(wsUrl);

      ws.onopen = () => {
        dot.style.background = 'var(--success)';
        txt.innerText = 'Connected';
      };
      ws.onclose = () => {
        dot.style.background = 'var(--danger)';
        txt.innerText = 'Disconnected — retrying...';
        setTimeout(connectWS, 3000);
      };
      ws.onerror = () => ws.close();

      ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          if (msg.type === 'history') {
            // Initial history dump
            msg.data.forEach(e => { logBuffer.push(e); appendLogLine(e, false); });
            document.getElementById('logContainer').scrollTop = document.getElementById('logContainer').scrollHeight;
          } else {
            // Live entry: {ts, lvl, msg}
            logBuffer.push(msg);
            if (logBuffer.length > 200) logBuffer.shift();
            appendLogLine(msg);
          }
        } catch(e) { console.error('WS parse error', e); }
      };
    }
    connectWS();
  </script>
</body>
</html>
)rawliteral";



static bool checkAuth(AsyncWebServerRequest *request) {
    if (!request->authenticate(SECRET_DASHBOARD_USER, SECRET_DASHBOARD_PASS)) {
        request->requestAuthentication();
        return false;
    }
    return true;
}

void WebDashboard::init(SmartBoxController& controller) {
    controllerPtr = &controller;

    // ── WebSocket log endpoint setup ──────────────────────────
    wsLog.onEvent(RemoteLogger::onWsEvent);
    server.addHandler(&wsLog);
    RemoteLogger::init(&wsLog);
    Serial.println("[DASHBOARD] WebSocket log endpoint active: ws://<ip>/api/logs");

    // ── Log history REST endpoint (for non-WS clients) ────────
    server.on("/api/logs/history", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        String json = RemoteLogger::getHistoryJson();
        request->send(200, "application/json", json);
    });

    // Serve HTML Dashboard
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        request->send_P(200, "text/html", INDEX_HTML);
    });
    
    // Serve Telemetry State JSON API
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr == nullptr) {
            request->send(500, "application/json", "{\"error\":\"Controller uninitialized\"}");
            return;
        }
        
        String stateStr;
        switch(controllerPtr->getCurrentState()) {
            case STATE_IDLE: stateStr = "IDLE"; break;
            case STATE_OPEN_START: stateStr = "OPEN_START"; break;
            case STATE_OPENING: stateStr = "OPENING"; break;
            case STATE_HOLD: stateStr = "HOLD"; break;
            case STATE_CLOSE_START: stateStr = "CLOSE_START"; break;
            case STATE_CLOSING: stateStr = "CLOSING"; break;
            case STATE_EMERGENCY_STOP: stateStr = "EMERGENCY_STOP"; break;
            case STATE_BATTERY_LOW_SHUTDOWN: stateStr = "BATTERY_LOW_SHUTDOWN"; break;
            case STATE_STARTUP_OPEN: stateStr = "STARTUP_OPEN"; break;
            case STATE_OTA_UPDATING: stateStr = "OTA_UPDATING"; break;
            case STATE_MAINTENANCE: stateStr = "MAINTENANCE"; break;
            default: stateStr = "UNKNOWN"; break;
        }
        
        BoxConfig cfg = controllerPtr->getConfig();
        
        // Asynchronously craft JSON without heavy external dependencies
        String json;
        json.reserve(512);
        json = "{";
        json += "\"state\":\"" + stateStr + "\",";
        json += "\"version\":\"" + String(controllerPtr->getFirmwareVersion()) + "\",";
        json += "\"wifiConnected\":" + String(WifiManager::isConnected() ? "true" : "false") + ",";
        json += "\"battery\":" + String(controllerPtr->getBatteryVoltage(), 2) + ",";
        json += "\"current\":" + String(controllerPtr->getMotorCurrent(), 2) + ",";
        json += "\"distance\":" + String(controllerPtr->getDistance(), 1) + ",";
        json += "\"time\":" + String(millis()) + ","; // Time placeholder
        json += "\"firmwareUrl\":\"" + escapeJson(String(SECRET_FIRMWARE_URL)) + "\",";
        json += "\"maintenance\":" + String(controllerPtr->getCurrentState() == STATE_MAINTENANCE ? "true" : "false") + ",";
        json += "\"maintenanceTime\":" + String(controllerPtr->getMaintenanceRemainingSeconds()) + ",";
        json += "\"otaState\":\"" + AutoOtaManager::getOtaStateString() + "\",";
        json += "\"otaError\":\"" + escapeJson(AutoOtaManager::getOtaErrorMessage()) + "\",";
        json += "\"config\":{";
        json += "\"dist\":" + String(cfg.distThreshold, 1) + ",";
        json += "\"wait\":" + String(cfg.waitTime) + ",";
        json += "\"stall\":" + String(cfg.currentStallLimit, 1) + ",";
        json += "\"otaHour\":" + String(cfg.otaHour) + ",";
        json += "\"telInterval\":" + String(cfg.telemetryIntervalMin);
        json += "}";
        json += "}";
        
        request->send(200, "application/json", json);
    });
    
    // Release Emergency Lock endpoint
    server.on("/api/release", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr != nullptr) {
            controllerPtr->resetEmergency();
            request->send(200, "application/json", "{\"status\":\"released\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\"}");
        }
    });
    
    // Remote Force Open endpoint
    server.on("/api/open", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr != nullptr) {
            controllerPtr->forceOpen();
            request->send(200, "application/json", "{\"status\":\"opening\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\"}");
        }
    });
    
    // Remote Force Close endpoint
    server.on("/api/close", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr != nullptr) {
            controllerPtr->forceClose();
            request->send(200, "application/json", "{\"status\":\"closing\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\"}");
        }
    });
    
    // Manual Maintenance Mode endpoint
    server.on("/api/maintenance", HTTP_ANY, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr == nullptr) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Controller null\"}");
            return;
        }
        if (request->hasParam("action")) {
            String action = request->getParam("action")->value();
            if (action == "start") {
                controllerPtr->startMaintenanceMode();
                request->send(200, "application/json", "{\"status\":\"started\"}");
            } else if (action == "stop") {
                controllerPtr->stopMaintenanceMode();
                request->send(200, "application/json", "{\"status\":\"stopped\"}");
            } else {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid action\"}");
            }
        } else {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing action param\"}");
        }
    });
    
    // Dynamic sensitivity calibration endpoint
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr == nullptr) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Controller null\"}");
            return;
        }
        
        BoxConfig cfg = controllerPtr->getConfig();
        
        if (request->hasParam("dist")) {
            cfg.distThreshold = request->getParam("dist")->value().toFloat();
        }
        if (request->hasParam("wait")) {
            cfg.waitTime = request->getParam("wait")->value().toInt();
        }
        if (request->hasParam("stall")) {
            cfg.currentStallLimit = request->getParam("stall")->value().toFloat();
        }
        if (request->hasParam("otaHour")) {
            cfg.otaHour = request->getParam("otaHour")->value().toInt();
            if (cfg.otaHour < 0 || cfg.otaHour > 23) {
                cfg.otaHour = 3;
            }
        }
        if (request->hasParam("telInterval")) {
            cfg.telemetryIntervalMin = request->getParam("telInterval")->value().toInt();
            if (cfg.telemetryIntervalMin < 1 || cfg.telemetryIntervalMin > 1440) {
                cfg.telemetryIntervalMin = 10;
            }
        }
        
        // Bind to controller
        controllerPtr->setConfig(cfg);
        
        // Write to Flash memory persistently
        ConfigManager::saveConfig(cfg);
        
        request->send(200, "application/json", "{\"status\":\"updated\"}");
    });

    // ===== Asynchronous Wi-Fi Scan Endpoint =====
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        bool forceStart = request->hasParam("start");
        int status = WifiManager::getScanStatus();
        
        if (forceStart) {
            if (status >= 0) {
                WiFi.scanDelete();
            }
            WifiManager::startScan();
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else if (status == -1) { // WIFI_SCAN_RUNNING
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else if (status == -2) { // WIFI_SCAN_FAILED
            request->send(200, "application/json", "{\"status\":\"failed\",\"message\":\"Scan failed\"}");
        } else {
            String results = WifiManager::getScanResultsJson();
            String json;
            json.reserve(results.length() + 64);
            json = "{\"status\":\"complete\",\"networks\":" + results + "}";
            request->send(200, "application/json", json);
        }
    });

    // ===== Wi-Fi Provisioning Connect Endpoint =====
    server.on("/api/wifi/connect", HTTP_ANY, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        String ssid = "";
        String password = "";
        
        if (request->hasParam("ssid", request->method() == HTTP_POST)) {
            ssid = request->getParam("ssid", request->method() == HTTP_POST)->value();
        } else if (request->hasParam("ssid")) {
            ssid = request->getParam("ssid")->value();
        }
        
        if (request->hasParam("password", request->method() == HTTP_POST)) {
            password = request->getParam("password", request->method() == HTTP_POST)->value();
        } else if (request->hasParam("password")) {
            password = request->getParam("password")->value();
        }
        
        if (ssid.isEmpty()) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SSID is required\"}");
            return;
        }
        
        ConfigManager::saveWifiCredentials(ssid, password);
        if (controllerPtr != nullptr && controllerPtr->isNightSleepActive()) {
            controllerPtr->setNightSleepMode(false);
#ifndef NATIVE_BUILD
            setCpuFrequencyMhz(160);
#endif
        }
        WifiManager::connectTo(ssid.c_str(), password.c_str());
        
        request->send(200, "application/json", "{\"status\":\"connecting\",\"message\":\"Initiating connection\"}");
    });
    
    // ===== NAS HTTPS Pull OTA Endpoint =====
    server.on("/api/update-from-nas", HTTP_GET, [](AsyncWebServerRequest *request){
        if (!checkAuth(request)) return;
        if (controllerPtr == nullptr) {
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Controller uninitialized\"}");
            return;
        }
        
        if (AutoOtaManager::startOtaUpdate(true)) {
            Serial.println("[OTA-NAS] Triggered HTTPS OTA from NAS (manual force).");
            request->send(200, "application/json", "{\"status\":\"started\",\"message\":\"NAS HTTPS OTA triggered\"}");
        } else {
            Serial.println("[OTA-NAS] Failed to trigger HTTPS OTA.");
            request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to start OTA task\"}");
        }
    });
    
    // ===== Captive Portal Redirection onNotFound Handler =====
    server.onNotFound([](AsyncWebServerRequest *request){
        String host = request->host();
        String apIP = WiFi.softAPIP().toString();
        if (host != apIP) {
            Serial.printf("[DASHBOARD] Redirecting captive request (Host: %s) to: http://%s/\n", host.c_str(), apIP.c_str());
            request->redirect("http://" + apIP + "/");
        } else {
            request->send(404, "text/plain", "Not Found");
        }
    });
    
    server.begin();
    Serial.println("[DASHBOARD] Asynchronous Web Dashboard Server active on port 80.");
}

void WebDashboard::update() {
    // Under asynchronous webserver, network parsing runs on background threads, 
    // hence no heavy update loops are required here.
}
