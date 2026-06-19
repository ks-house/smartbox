#include "WebDashboard.h"
#include "ConfigManager.h"
#include "WifiManager.h"
#include "AutoOtaManager.h"
#include <Arduino.h>

AsyncWebServer WebDashboard::server(80);
SmartBoxController* WebDashboard::controllerPtr = nullptr;

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
    <button class='btn btn-primary' id='btnOpen' onclick='forceOpen()'>Remote Force Open</button>

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
        <input type='text' id='nasUrl' class='form-control' value='***REMOVED***' readonly style='background: rgba(255,255,255,0.01); color: var(--text-muted);'>
      </div>
      <div id='nasOtaStatus' style='display:none; padding: 12px; border-radius: 12px; margin-bottom: 15px; font-weight: 600; text-align: center;'></div>
      <button class='btn btn-primary' id='btnNasOta' onclick='updateFromNas()' style='background: var(--info); box-shadow: 0 8px 20px rgba(10,132,255,0.2);'>Fetch & Update</button>
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
          configLoaded = true;
        }
        
        card.className = 'card status-card';
        btnRelease.className = 'btn btn-disabled';
        btnRelease.disabled = true;
        
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
    
    async function updateConfig() {
      const dist = document.getElementById('cfgDistance').value;
      const wait = document.getElementById('cfgWait').value;
      const stall = document.getElementById('cfgStall').value;
      
      try {
        const res = await fetch(`/api/config?dist=${dist}&wait=${wait}&stall=${stall}`);
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
      statusDiv.innerText = '⏳ NAS로부터 업데이트를 다운로드 중입니다. 전원을 끄지 마세요...';
      btn.disabled = true;
      btn.className = 'btn btn-disabled';
      
      try {
        const res = await fetch('/api/update-from-nas');
        const result = await res.json();
        if (result.status === 'started') {
          // Poll /api/status to see if it changes to OTA_UPDATING, and wait for disconnect or reboot
          let checkInterval = setInterval(async () => {
            try {
              const checkRes = await fetch('/api/status');
              const checkData = await checkRes.json();
              if (checkData.state === 'OTA_UPDATING') {
                statusDiv.innerText = '⏳ NAS로부터 업데이트를 다운로드 중입니다. 전원을 끄지 마세요... (플래싱 중)';
              } else if (checkData.state === 'IDLE') {
                // If it returned to IDLE, OTA must have failed
                clearInterval(checkInterval);
                statusDiv.style.background = 'rgba(239,68,68,0.15)';
                statusDiv.style.color = 'var(--danger)';
                statusDiv.innerText = '❌ NAS OTA 실패: 플래시 쓰기 오류 또는 네트워크 연결 실패';
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
      } catch (e) {
        statusDiv.style.background = 'rgba(239,68,68,0.15)';
        statusDiv.style.color = 'var(--danger)';
        statusDiv.innerText = '❌ 서버와 통신 실패.';
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
  </script>
</body>
</html>
)rawliteral";



void WebDashboard::init(SmartBoxController& controller) {
    controllerPtr = &controller;
    
    // Serve HTML Dashboard
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", INDEX_HTML);
    });
    
    // Serve Telemetry State JSON API
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
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
            default: stateStr = "UNKNOWN"; break;
        }
        
        BoxConfig cfg = controllerPtr->getConfig();
        
        // Asynchronously craft JSON without heavy external dependencies
        String json = "{";
        json += "\"state\":\"" + stateStr + "\",";
        json += "\"version\":\"" + String(controllerPtr->getFirmwareVersion()) + "\",";
        json += "\"wifiConnected\":" + String(WifiManager::isConnected() ? "true" : "false") + ",";
        json += "\"battery\":" + String(controllerPtr->getBatteryVoltage(), 2) + ",";
        json += "\"current\":" + String(controllerPtr->getMotorCurrent(), 2) + ",";
        json += "\"distance\":" + String(controllerPtr->getDistance(), 1) + ",";
        json += "\"time\":" + String(millis()) + ","; // Time placeholder
        json += "\"config\":{";
        json += "\"dist\":" + String(cfg.distThreshold, 1) + ",";
        json += "\"wait\":" + String(cfg.waitTime) + ",";
        json += "\"stall\":" + String(cfg.currentStallLimit, 1);
        json += "}";
        json += "}";
        
        request->send(200, "application/json", json);
    });
    
    // Release Emergency Lock endpoint
    server.on("/api/release", HTTP_GET, [](AsyncWebServerRequest *request){
        if (controllerPtr != nullptr) {
            controllerPtr->resetEmergency();
            request->send(200, "application/json", "{\"status\":\"released\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\"}");
        }
    });
    
    // Remote Force Open endpoint
    server.on("/api/open", HTTP_GET, [](AsyncWebServerRequest *request){
        if (controllerPtr != nullptr) {
            controllerPtr->forceOpen();
            request->send(200, "application/json", "{\"status\":\"opening\"}");
        } else {
            request->send(500, "application/json", "{\"status\":\"error\"}");
        }
    });
    
    // Dynamic sensitivity calibration endpoint
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
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
        
        // Bind to controller
        controllerPtr->setConfig(cfg);
        
        // Write to Flash memory persistently
        ConfigManager::saveConfig(cfg);
        
        request->send(200, "application/json", "{\"status\":\"updated\"}");
    });

    // ===== Asynchronous Wi-Fi Scan Endpoint =====
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request){
        bool forceStart = request->hasParam("start");
        int status = WifiManager::getScanStatus();
        if (forceStart || status == -2) {
            if (status >= 0) {
                WiFi.scanDelete();
            }
            WifiManager::startScan();
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else if (status == -1) {
            request->send(200, "application/json", "{\"status\":\"scanning\"}");
        } else {
            String results = WifiManager::getScanResultsJson();
            String json = "{\"status\":\"complete\",\"networks\":" + results + "}";
            request->send(200, "application/json", json);
        }
    });

    // ===== Wi-Fi Provisioning Connect Endpoint =====
    server.on("/api/wifi/connect", HTTP_ANY, [](AsyncWebServerRequest *request){
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
        WifiManager::connectTo(ssid.c_str(), password.c_str());
        
        request->send(200, "application/json", "{\"status\":\"connecting\",\"message\":\"Initiating connection\"}");
    });
    
    // ===== NAS HTTPS Pull OTA Endpoint =====
    server.on("/api/update-from-nas", HTTP_GET, [](AsyncWebServerRequest *request){
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
    
    server.begin();
    Serial.println("[DASHBOARD] Asynchronous Web Dashboard Server active on port 80.");
}

void WebDashboard::update() {
    // Under asynchronous webserver, network parsing runs on background threads, 
    // hence no heavy update loops are required here.
}
