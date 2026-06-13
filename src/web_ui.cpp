#include "web_ui.h"
#include "config.h"
#include "github_update.h"
#include "selfupdate.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static WebServer server(WEB_PORT);
static File      uploadFile;

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OSOS bridge</title>
<style>
  body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}
  header{padding:12px 16px;background:#171a21;font-weight:600}
  .wrap{padding:16px;max-width:640px;margin:0 auto}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:8px}
  .card{background:#171a21;border:1px solid #262b35;border-radius:8px;padding:10px}
  .k{font-size:11px;color:#8a93a6;text-transform:uppercase}
  .v{font-size:18px;margin-top:2px}
  button{background:#2563eb;color:#fff;border:0;border-radius:6px;padding:8px 12px;
         margin:2px;cursor:pointer;font-size:13px}
  button.sec{background:#374151}
  input[type=file]{color:#e6e6e6}
  h3{margin:18px 0 8px}
</style></head><body>
<header>OSOS &mdash; OpenSpand WiFi bridge</header>
<div class="wrap">
  <h3>Status</h3>
  <div class="grid" id="status"></div>

  <h3>Send a program to the ZX81</h3>
  <div class="card">
    <form id="up" onsubmit="return upload(event)">
      <input type="file" id="file" accept=".p,.P" required>
      <button type="submit">Upload .p</button>
      <span id="up_msg" style="margin-left:8px;font-size:13px"></span>
    </form>
    <div class="k" style="margin-top:8px">Then press <b>S</b> on the ZX81 to pull it (saved as INBOX.P).</div>
  </div>

  <h3>Updates</h3>
  <div class="card">
    <button onclick="post('/api/menucheck','m')">Check for new menu.p</button>
    <button class="sec" onclick="post('/api/fwupdate','m')">Check ESP firmware</button>
    <span id="m" style="margin-left:8px;font-size:13px"></span>
    <div class="k" style="margin-top:8px">menu.p is mirrored from GitHub; the ZX81 installs it via <b>U</b> (UPDATE OS).</div>
  </div>
</div>
<script>
function card(k,v){return '<div class=card><div class=k>'+k+'</div><div class=v>'+v+'</div></div>'}
async function tick(){
  try{
    let s=await (await fetch('/api/state')).json();
    document.getElementById('status').innerHTML=
      card('WiFi',s.ssid+' ('+s.rssi+' dBm)')+
      card('IP',s.ip)+
      card('Bridge FW',s.fw)+
      card('Program armed',s.program_size?(s.program_size+' B'):'none')+
      card('menu.p mirror',s.menu_version?('v'+s.menu_version):'none');
  }catch(e){}
}
async function upload(ev){
  ev.preventDefault();
  let f=document.getElementById('file').files[0];
  let el=document.getElementById('up_msg');
  if(!f){return false}
  el.textContent='uploading…';
  let fd=new FormData(); fd.append('f',f,f.name);
  try{let r=await fetch('/upload',{method:'POST',body:fd});
      el.textContent=r.ok?('armed '+f.name+' ✓'):'error';}
  catch(e){el.textContent='error';}
  tick(); return false;
}
async function post(u,msgId){
  let el=document.getElementById(msgId); el.textContent='checking…';
  try{let r=await fetch(u,{method:'POST'}); el.textContent=await r.text();}
  catch(e){el.textContent='(rebooting if updating…)';}
  tick();
}
setInterval(tick,2000); tick();
</script></body></html>)HTML";

static void handleState() {
  JsonDocument d;
  d["ssid"] = WiFi.SSID();
  d["ip"]   = WiFi.localIP().toString();
  d["rssi"] = WiFi.RSSI();
  d["fw"]   = FIRMWARE_VERSION_STR;
  uint32_t psz = 0;
  if (LittleFS.exists(PROGRAM_FS_PATH)) {
    File f = LittleFS.open(PROGRAM_FS_PATH, "r");
    if (f) { psz = f.size(); f.close(); }
  }
  d["program_size"] = psz;
  d["menu_version"] = githubUpdateMenuVersion();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

static void handleUploadData() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadFile = LittleFS.open(PROGRAM_FS_PATH, "w");
    Serial.printf("[web] upload start: %s\n", up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) uploadFile.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (uploadFile) uploadFile.close();
    Serial.printf("[web] upload done: %u bytes\n", (unsigned)up.totalSize);
  }
}

static void registerRoutes() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", PAGE_HTML); });
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "ok"); },
            handleUploadData);
  server.on("/api/menucheck", HTTP_POST, []() {
    server.send(200, "text/plain", githubUpdateCheckNow());
  });
  server.on("/api/fwupdate", HTTP_POST, []() {
    server.send(200, "text/plain", selfUpdateCheckNow());   // reboots on success
  });
}

void webUiBegin() {
  registerRoutes();
  server.begin();
  Serial.printf("[web] UI on http://%s.local/  (port %d)\n", MDNS_HOSTNAME, WEB_PORT);
}

void webUiLoop()   { server.handleClient(); }
void webUiStop()   { server.close(); }
void webUiResume() { server.begin(); }
