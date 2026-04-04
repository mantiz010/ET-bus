#include "ETBusWiFiManager.h"

// ══════════════════════════════════════════════════════════════
// Branded portal HTML (stored in flash via PROGMEM)
// ══════════════════════════════════════════════════════════════
static const char ETBUS_PORTAL_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>ET-Bus Setup</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{
--blue:#1a6fd4;--blue-l:#2b80e2;--blue-d:#1259b0;--blue-glow:rgba(26,111,212,.15);
--green:#00ff9d;--red:#ff3d5a;
--bg0:#04080c;--bg1:#0a1018;--bg2:#111a24;--bg3:#182430;
--brd:#1e2f3d;--t0:#e8f4f8;--t1:#94b8c9;--t2:#5a7d8f;--t3:#3a5565;
}
body{font-family:'Segoe UI',system-ui,sans-serif;background:var(--bg0);color:var(--t0);
min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}
body::after{content:'';position:fixed;inset:0;pointer-events:none;z-index:9998;
background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,0,0,.03) 2px,rgba(0,0,0,.03) 4px)}
.box{width:100%;max-width:420px}
.logo{text-align:center;margin-bottom:.5rem;font-size:2.5rem;font-weight:800;letter-spacing:-2px}
.logo .et{color:var(--blue);text-shadow:0 0 30px rgba(26,111,212,.5)}
.logo .bus{color:#ffffff}
.sub{text-align:center;font-size:.65rem;color:var(--t3);letter-spacing:3px;text-transform:uppercase;margin-bottom:2rem}
.card{background:var(--bg1);border:1px solid var(--brd);border-radius:14px;padding:1.5rem;margin-bottom:1rem}
.card-title{font-size:.65rem;color:var(--t3);letter-spacing:2px;text-transform:uppercase;margin-bottom:1rem;display:flex;align-items:center;gap:.5rem}
.card-title::before{content:'';flex:0 0 3px;height:14px;background:var(--blue);border-radius:2px}
.field{margin-bottom:.85rem}
.field:last-child{margin-bottom:0}
.field label{display:block;font-size:.7rem;color:var(--t2);letter-spacing:1px;text-transform:uppercase;margin-bottom:.3rem}
.field input{width:100%;background:var(--bg2);border:1px solid var(--brd);border-radius:8px;padding:.7rem .85rem;color:var(--t0);font-size:.85rem;transition:border .2s}
.field input:focus{outline:none;border-color:var(--blue);box-shadow:0 0 0 3px var(--blue-glow)}
.field input::placeholder{color:var(--t3)}
.field-hint{font-size:.6rem;color:var(--t3);margin-top:.3rem;line-height:1.4}
.pw-wrap{position:relative}
.pw-wrap input{padding-right:3rem}
.pw-toggle{position:absolute;right:.5rem;top:50%;transform:translateY(-50%);background:none;border:none;color:var(--t2);font-size:1.1rem;cursor:pointer;padding:.25rem}
.pw-toggle:hover{color:var(--blue)}
.wifi-list{max-height:200px;overflow-y:auto;margin-bottom:.5rem;-webkit-overflow-scrolling:touch}
.wifi-list::-webkit-scrollbar{width:4px}
.wifi-list::-webkit-scrollbar-thumb{background:var(--brd);border-radius:2px}
.wifi-item{display:flex;align-items:center;justify-content:space-between;padding:.6rem .75rem;background:var(--bg2);border:1px solid var(--brd);border-radius:8px;margin-bottom:.35rem;cursor:pointer;transition:all .2s}
.wifi-item:hover{border-color:var(--blue);background:var(--bg3)}
.wifi-item.selected{border-color:var(--blue);background:rgba(26,111,212,.08)}
.wifi-name{font-size:.85rem;font-weight:600;color:var(--t0)}
.wifi-signal{display:flex;gap:2px;align-items:flex-end}
.wifi-bar{width:3px;background:var(--t3);border-radius:1px}
.wifi-item:hover .wifi-bar,.wifi-item.selected .wifi-bar{background:var(--blue)}
.btn{width:100%;padding:.85rem;background:linear-gradient(135deg,var(--blue-d),var(--blue-l));border:none;border-radius:8px;color:#fff;font-size:.85rem;font-weight:700;letter-spacing:2px;text-transform:uppercase;cursor:pointer;transition:all .3s;margin-top:.5rem}
.btn:hover{transform:translateY(-2px);box-shadow:0 8px 30px rgba(26,111,212,.35)}
.btn:disabled{opacity:.4;cursor:not-allowed;transform:none}
.btn-scan{background:var(--bg2);border:1px solid var(--brd);color:var(--t1);font-size:.7rem;letter-spacing:1px;padding:.5rem .85rem;width:auto;margin:0;cursor:pointer;border-radius:8px}
.btn-scan:hover{border-color:var(--blue);color:var(--blue)}
.status{text-align:center;padding:1rem;font-size:.8rem;border-radius:8px;margin-top:.75rem;display:none}
.status.info{display:block;background:rgba(26,111,212,.08);color:var(--blue-l);border:1px solid rgba(26,111,212,.2)}
.status.ok{display:block;background:rgba(0,255,157,.08);color:var(--green);border:1px solid rgba(0,255,157,.2)}
.status.err{display:block;background:rgba(255,61,90,.08);color:var(--red);border:1px solid rgba(255,61,90,.2)}
.hint{font-size:.65rem;color:var(--t3);text-align:center;margin-top:1.25rem;line-height:1.5}
.ver{text-align:center;font-size:.55rem;color:var(--t3);margin-top:.75rem;letter-spacing:1px}
@media(max-width:480px){.box{max-width:100%}.card{padding:1.1rem}.logo{font-size:2rem}}
</style>
</head>
<body>
<div class="box">
  <div class="logo"><span class="et">ET</span><span class="bus">-Bus</span></div>
  <div class="sub">Device Setup Portal</div>
  <div class="card">
    <div class="card-title">WiFi Configuration</div>
    <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:.75rem">
      <span style="font-size:.7rem;color:var(--t2)" id="scan-status">Scanning&#8230;</span>
      <button class="btn-scan" onclick="scanWifi()">&#8635; RESCAN</button>
    </div>
    <div class="wifi-list" id="wifi-list"></div>
    <div class="field">
      <label>SSID</label>
      <input type="text" id="ssid" placeholder="Network name" autocomplete="off">
    </div>
    <div class="field">
      <label>Password</label>
      <div class="pw-wrap">
        <input type="password" id="pass" placeholder="WiFi password" autocomplete="off">
        <button class="pw-toggle" onclick="togglePw('pass',this)">&#x1f441;</button>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="card-title">&#x1f510; Encryption</div>
    <div class="field">
      <label>Pre-Shared Key (64 hex chars)</label>
      <div class="pw-wrap">
        <input type="password" id="psk" placeholder="b6f0c3d7a12e4f9c..." autocomplete="off">
        <button class="pw-toggle" onclick="togglePw('psk',this)">&#x1f441;</button>
      </div>
      <div class="field-hint">ChaCha20-Poly1305 &#183; Must match your ET-Bus hub key</div>
    </div>
  </div>
  <div class="card">
    <div class="card-title">Device</div>
    <div class="field">
      <label>Device Name</label>
      <input type="text" id="dev-name" placeholder="e.g. living_room_relay" autocomplete="off">
    </div>
  </div>
  <button class="btn" id="btn-save" onclick="saveConfig()">SAVE &amp; CONNECT</button>
  <div class="status" id="status"></div>
  <div class="hint">After saving, the device will connect to your WiFi<br>and begin encrypted communication with ET-Bus.</div>
  <div class="ver">ET-Bus WiFi Manager v1.0</div>
</div>
<script>
let selectedSSID='';
function scanWifi(){
document.getElementById('scan-status').textContent='Scanning\u2026';
document.getElementById('wifi-list').innerHTML='';
fetch('/scan').then(r=>r.json()).then(nets=>{
const el=document.getElementById('wifi-list');
if(!nets.length){el.innerHTML='<div style="text-align:center;padding:.75rem;color:var(--t3);font-size:.75rem">No networks found</div>';document.getElementById('scan-status').textContent='0 networks';return}
document.getElementById('scan-status').textContent=nets.length+' networks';
nets.forEach(n=>{const d=document.createElement('div');d.className='wifi-item'+(n.ssid===selectedSSID?' selected':'');d.onclick=()=>selectWifi(n.ssid);
const bars=sigBars(n.rssi);const lock=n.enc?'\u{1f512} ':'';
d.innerHTML='<span class="wifi-name">'+lock+esc(n.ssid)+'</span><div class="wifi-signal">'+bars+'</div>';el.appendChild(d)})
}).catch(()=>{document.getElementById('scan-status').textContent='Scan failed'})}
function selectWifi(s){selectedSSID=s;document.getElementById('ssid').value=s;
document.querySelectorAll('.wifi-item').forEach(e=>e.classList.remove('selected'));
document.querySelectorAll('.wifi-item').forEach(e=>{if(e.querySelector('.wifi-name').textContent.replace('\u{1f512} ','')===s)e.classList.add('selected')});
document.getElementById('pass').focus()}
function sigBars(rssi){const s=rssi>-50?4:rssi>-65?3:rssi>-75?2:1;let h='';
for(let i=1;i<=4;i++){const ht=i*4+2;const a=i<=s;h+='<div class="wifi-bar" style="height:'+ht+'px;'+(a?'background:var(--blue)':'')+'"></div>'}return h}
function togglePw(id,btn){const i=document.getElementById(id);if(i.type==='password'){i.type='text';btn.textContent='\u{1f512}'}else{i.type='password';btn.textContent='\u{1f441}'}}
function saveConfig(){
const ssid=document.getElementById('ssid').value.trim();
const pass=document.getElementById('pass').value;
const psk=document.getElementById('psk').value.trim();
const devName=document.getElementById('dev-name').value.trim();
if(!ssid){showSt('Please select or enter a WiFi network.','err');return}
if(psk&&psk.length!==64){showSt('PSK must be exactly 64 hex characters.','err');return}
if(psk&&!/^[0-9a-fA-F]+$/.test(psk)){showSt('PSK must contain only hex characters (0-9, a-f).','err');return}
document.getElementById('btn-save').disabled=true;document.getElementById('btn-save').textContent='SAVING\u2026';
showSt('Saving configuration\u2026','info');
fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({ssid,pass,psk,dev_name:devName})
}).then(r=>r.json()).then(res=>{
if(res.ok){showSt('\u2705 Saved! Connecting to WiFi\u2026 This page will close.','ok');
setTimeout(()=>{showSt('Rebooting device\u2026','info')},3000)}
else{showSt('Save failed: '+(res.error||'unknown'),'err');document.getElementById('btn-save').disabled=false;document.getElementById('btn-save').textContent='SAVE & CONNECT'}
}).catch(e=>{showSt('Error: '+e.message,'err');document.getElementById('btn-save').disabled=false;document.getElementById('btn-save').textContent='SAVE & CONNECT'})}
function showSt(m,t){const e=document.getElementById('status');e.textContent=m;e.className='status '+t}
function esc(s){const d=document.createElement('div');d.textContent=s;return d.innerHTML}
scanWifi();
fetch('/current').then(r=>r.json()).then(d=>{
if(d.ssid)document.getElementById('ssid').value=d.ssid;
if(d.dev_name)document.getElementById('dev-name').value=d.dev_name}).catch(()=>{});
</script>
</body>
</html>
)rawhtml";


// ══════════════════════════════════════════════════════════════
// Public methods
// ══════════════════════════════════════════════════════════════

void ETBusWiFiManager::begin(const char* apName) {
    _apName = String(apName);
    _loadNVS();

    if (_ssid.length() > 0) {
        Serial.printf("[WM] Connecting to: %s\n", _ssid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(_ssid.c_str(), _pass.c_str());

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WM] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
            return;
        }
        Serial.println("[WM] Connection failed");
    }

    _startPortal();
}

String ETBusWiFiManager::getSSID()     { return _ssid; }
String ETBusWiFiManager::getPassword() { return _pass; }
String ETBusWiFiManager::getPSK()      { return _psk; }
String ETBusWiFiManager::getDevName()  { return _devName; }

void ETBusWiFiManager::resetSettings() {
    Preferences prefs;
    prefs.begin("etbuswm", false);
    prefs.clear();
    prefs.end();
    _ssid = _pass = _psk = _devName = "";
    Serial.println("[WM] Settings cleared");
}

bool ETBusWiFiManager::isPortalActive() { return _portalActive; }


// ══════════════════════════════════════════════════════════════
// Private methods
// ══════════════════════════════════════════════════════════════

void ETBusWiFiManager::_loadNVS() {
    Preferences prefs;
    prefs.begin("etbuswm", true);
    _ssid    = prefs.getString("ssid", "");
    _pass    = prefs.getString("pass", "");
    _psk     = prefs.getString("psk", "");
    _devName = prefs.getString("dev_name", "");
    prefs.end();

    if (_ssid.length()) {
        Serial.printf("[WM] NVS: ssid=%s dev=%s psk=%s\n",
            _ssid.c_str(), _devName.c_str(), _psk.length() ? "set" : "none");
    } else {
        Serial.println("[WM] NVS: no saved config");
    }
}

void ETBusWiFiManager::_saveNVS() {
    Preferences prefs;
    prefs.begin("etbuswm", false);
    prefs.putString("ssid", _ssid);
    prefs.putString("pass", _pass);
    prefs.putString("psk", _psk);
    prefs.putString("dev_name", _devName);
    prefs.end();
    Serial.println("[WM] Config saved to NVS");
}

void ETBusWiFiManager::_startPortal() {
    Serial.printf("[WM] Starting portal AP: %s\n", _apName.c_str());
    _portalActive = true;

    WiFi.mode(WIFI_AP_STA);
    if (apPassword.length() >= 8)
        WiFi.softAP(_apName.c_str(), apPassword.c_str());
    else
        WiFi.softAP(_apName.c_str());

    delay(500);
    Serial.printf("[WM] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    // DNS — redirect all domains to our IP (captive portal)
    _dns = new DNSServer();
    _dns->start(53, "*", WiFi.softAPIP());

    // Web server
    _server = new WebServer(80);

    // Serve branded portal (chunked — too large for send_P)
    _server->on("/", HTTP_GET, [this]() {
        _sendHtml();
    });

    // WiFi scan endpoint
    _server->on("/scan", HTTP_GET, [this]() {
        int n = WiFi.scanNetworks();
        String json = "[";
        std::vector<String> seen;

        for (int i = 0; i < n; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            bool dup = false;
            for (auto& s : seen) if (s == ssid) { dup = true; break; }
            if (dup) continue;
            seen.push_back(ssid);

            if (json.length() > 1) json += ",";
            json += "{\"ssid\":\"" + _jsonEsc(ssid) + "\","
                    "\"rssi\":" + String(WiFi.RSSI(i)) + ","
                    "\"enc\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
        }
        json += "]";
        WiFi.scanDelete();

        _server->sendHeader("Access-Control-Allow-Origin", "*");
        _server->send(200, "application/json", json);
    });

    // Current config (don't expose passwords/keys)
    _server->on("/current", HTTP_GET, [this]() {
        String json = "{\"ssid\":\"" + _jsonEsc(_ssid) + "\","
                      "\"dev_name\":\"" + _jsonEsc(_devName) + "\"}";
        _server->sendHeader("Access-Control-Allow-Origin", "*");
        _server->send(200, "application/json", json);
    });

    // Save config
    _server->on("/save", HTTP_POST, [this]() {
        if (!_server->hasArg("plain")) {
            _server->send(400, "application/json", "{\"ok\":false,\"error\":\"no body\"}");
            return;
        }

        String body = _server->arg("plain");
        _ssid    = _extractJson(body, "ssid");
        _pass    = _extractJson(body, "pass");
        _psk     = _extractJson(body, "psk");
        _devName = _extractJson(body, "dev_name");

        if (_ssid.length() == 0) {
            _server->send(400, "application/json", "{\"ok\":false,\"error\":\"SSID required\"}");
            return;
        }
        if (_psk.length() > 0 && _psk.length() != 64) {
            _server->send(400, "application/json", "{\"ok\":false,\"error\":\"PSK must be 64 hex chars\"}");
            return;
        }

        _saveNVS();

        _server->sendHeader("Access-Control-Allow-Origin", "*");
        _server->send(200, "application/json", "{\"ok\":true}");

        delay(2000);
        Serial.println("[WM] Rebooting...");
        ESP.restart();
    });

    // Captive portal detection endpoints (Android/iOS/Windows)
    _server->on("/generate_204", HTTP_GET, [this]() {
        _server->sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        _server->send(302);
    });
    _server->on("/fwlink", HTTP_GET, [this]() {
        _server->sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        _server->send(302);
    });
    _server->on("/hotspot-detect.html", HTTP_GET, [this]() {
        _sendHtml();
    });
    _server->on("/connecttest.txt", HTTP_GET, [this]() {
        _server->sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        _server->send(302);
    });

    // Catch-all → redirect to portal
    _server->onNotFound([this]() {
        _server->sendHeader("Location", "http://" + WiFi.softAPIP().toString());
        _server->send(302);
    });

    _server->begin();
    Serial.println("[WM] Portal started");

    // Block until save or timeout
    unsigned long start = millis();
    while (millis() - start < portalTimeout * 1000) {
        _dns->processNextRequest();
        _server->handleClient();
        delay(1);
    }

    Serial.println("[WM] Portal timeout — rebooting");
    _server->stop();
    _dns->stop();
    delete _server; _server = nullptr;
    delete _dns;    _dns = nullptr;
    _portalActive = false;
    ESP.restart();
}

void ETBusWiFiManager::_sendHtml() {
    size_t len = strlen_P(ETBUS_PORTAL_HTML);
    _server->setContentLength(len);
    _server->send(200, "text/html", "");

    // Send in 1KB chunks (send_P fails on large PROGMEM strings)
    const size_t CHUNK = 1024;
    char buf[CHUNK + 1];
    size_t sent = 0;

    while (sent < len) {
        size_t n = len - sent;
        if (n > CHUNK) n = CHUNK;
        memcpy_P(buf, ETBUS_PORTAL_HTML + sent, n);
        buf[n] = '\0';
        _server->sendContent(buf);
        sent += n;
    }
}

String ETBusWiFiManager::_extractJson(const String& json, const char* key) {
    String search = "\"" + String(key) + "\"";
    int idx = json.indexOf(search);
    if (idx < 0) return "";

    idx = json.indexOf(':', idx + search.length());
    if (idx < 0) return "";

    idx = json.indexOf('"', idx + 1);
    if (idx < 0) return "";

    int start = idx + 1;
    int end = start;
    while (end < (int)json.length()) {
        if (json.charAt(end) == '\\') { end += 2; continue; }
        if (json.charAt(end) == '"') break;
        end++;
    }
    return json.substring(start, end);
}

String ETBusWiFiManager::_jsonEsc(const String& s) {
    String r = s;
    r.replace("\\", "\\\\");
    r.replace("\"", "\\\"");
    r.replace("\n", "\\n");
    return r;
}
