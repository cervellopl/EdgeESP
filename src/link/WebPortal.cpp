#include "link/WebPortal.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SD.h>
#include <Preferences.h>

WebPortal g_portal;

static WebServer s_server(80);
static Preferences s_prefs;

static const char kIndex[] PROGMEM = R"HTML(<!doctype html><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>EdgeESP</title>
<style>
body{font:15px system-ui;margin:0;background:#111;color:#eee}
header{padding:14px 16px;background:#1b1b1b;border-bottom:1px solid #333;font-weight:600}
main{padding:16px;max-width:720px;margin:auto}
.k{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;margin-bottom:18px}
.c{background:#1b1b1b;border:1px solid #2c2c2c;border-radius:8px;padding:10px}
.c b{display:block;font-size:26px;color:#fd7e14}
.c span{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:.5px}
a{color:#fd7e14;text-decoration:none}
li{margin:6px 0;list-style:none;background:#1b1b1b;padding:10px;border-radius:6px;
   display:flex;justify-content:space-between;border:1px solid #2c2c2c}
ul{padding:0}
</style>
<header>EdgeESP</header><main>
<div class=k id=k></div>
<h3>Recorded rides</h3><ul id=f>loading...</ul>
<h3>Courses</h3><ul id=c>loading...</ul>
<form method=POST action=/course enctype=multipart/form-data>
<input type=file name=gpx accept=.gpx><button>Upload GPX</button></form>
<h3>Firmware</h3>
<form method=POST action=/update enctype=multipart/form-data>
<input type=file name=fw accept=.bin><button>Upload</button></form>
<script>
const F=[['spd','km/h'],['dist','km'],['time',''],['alt','m'],['hr','bpm'],['pwr','W'],['bat','%']];
async function tick(){
 try{const s=await (await fetch('/api/live')).json();
  document.getElementById('k').innerHTML=F.map(([k,u])=>
   `<div class=c><span>${k} ${u}</span><b>${s[k]}</b></div>`).join('');
 }catch(e){}
}
async function list(url,el,empty,dir){
 const l=await (await fetch(url)).json();
 document.getElementById(el).innerHTML=l.length?l.map(x=>
  `<li><a href="/dl?d=${dir}&f=${encodeURIComponent(x.n)}">${x.n}</a><span>${(x.s/1024).toFixed(0)} kB</span></li>`
 ).join(''):`<li>${empty}</li>`;
}
function files(){
 list('/api/files','f','no rides yet','rides');
 list('/api/courses','c','no courses - upload a .gpx below','courses');
}
tick();files();setInterval(tick,1000);
</script></main>)HTML";

static RideComputer* s_rc = nullptr;
static File s_upload;
static bool s_uploadOk = false;

// Resolve a client-supplied directory + filename to a real path, or refuse.
// Only the two known directories are reachable, and no component may traverse.
static bool safePath(const String& dir, const String& name, String& out) {
  if (name.isEmpty() || name.indexOf("..") >= 0 ||
      name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) return false;
  if      (dir == "courses") out = String(COURSE_DIR) + "/" + name;
  else if (dir.isEmpty() || dir == "rides") out = "/rides/" + name;
  else return false;
  return true;
}

static void sendListing(const char* path) {
  String out = "[";
  File dir = SD.open(path);
  if (dir) {
    bool first = true;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      const char* n = f.name();
      const char* base = strrchr(n, '/');
      if (base) n = base + 1;
      if (!first) out += ",";
      first = false;
      out += "{\"n\":\"";
      out += n;
      out += "\",\"s\":";
      out += String((uint32_t)f.size());
      out += "}";
    }
    dir.close();
  }
  out += "]";
  s_server.send(200, "application/json", out);
}

static String fmtTimer(uint32_t ms) {
  uint32_t s = ms / 1000;
  char b[16];
  snprintf(b, sizeof(b), "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
           (unsigned long)((s % 3600) / 60), (unsigned long)(s % 60));
  return String(b);
}

void WebPortal::setCredentials(const char* ssid, const char* pass) {
  s_prefs.begin("wifi", false);
  s_prefs.putString("ssid", ssid);
  s_prefs.putString("pass", pass);
  s_prefs.end();
}

void WebPortal::routes() {
  s_server.on("/", []() { s_server.send_P(200, "text/html", kIndex); });

  s_server.on("/api/live", []() {
    const RideState& s = s_rc->state();
    char b[320];
    snprintf(b, sizeof(b),
      "{\"spd\":\"%.1f\",\"dist\":\"%.2f\",\"time\":\"%s\",\"alt\":\"%.0f\","
      "\"hr\":\"%s\",\"pwr\":\"%s\",\"bat\":\"%u\"}",
      s.speed * 3.6f, s.distance / 1000.0, fmtTimer(s.movingMs).c_str(),
      isnan(s.altitude) ? 0 : s.altitude,
      s.hasHr ? String(s.hr).c_str() : "--",
      s.hasPwr ? String(s.power).c_str() : "--",
      s.batteryPct);
    s_server.send(200, "application/json", b);
  });

  s_server.on("/api/files",   []() { sendListing("/rides"); });
  s_server.on("/api/courses", []() { sendListing(COURSE_DIR); });

  s_server.on("/dl", []() {
    String path;
    if (!safePath(s_server.arg("d"), s_server.arg("f"), path)) {
      s_server.send(400, "text/plain", "bad name");
      return;
    }
    File f = SD.open(path, FILE_READ);
    if (!f) { s_server.send(404, "text/plain", "not found"); return; }
    s_server.sendHeader("Content-Disposition",
                        "attachment; filename=\"" + path.substring(path.lastIndexOf('/') + 1) + "\"");
    s_server.streamFile(f, "application/octet-stream");
    f.close();
  });

  // GPX course upload - the practical way to get a route onto the device
  // without pulling the card out of the case.
  s_server.on("/course", HTTP_POST,
    []() {
      s_server.sendHeader("Location", "/");
      s_server.send(302, "text/plain", s_uploadOk ? "uploaded" : "upload failed");
    },
    []() {
      HTTPUpload& up = s_server.upload();
      if (up.status == UPLOAD_FILE_START) {
        s_uploadOk = false;
        String name = up.filename;
        name.replace("/", "_");
        name.replace("\\", "_");
        if (!name.endsWith(".gpx") && !name.endsWith(".GPX")) return;
        if (!SD.exists(COURSE_DIR)) SD.mkdir(COURSE_DIR);
        s_upload = SD.open(String(COURSE_DIR) + "/" + name, FILE_WRITE);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (s_upload) s_upload.write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END) {
        if (s_upload) { s_upload.close(); s_uploadOk = true; }
      }
    });

  s_server.on("/update", HTTP_POST,
    []() {
      s_server.send(200, "text/plain", Update.hasError() ? "FAIL" : "OK - rebooting");
      delay(400);
      ESP.restart();
    },
    []() {
      HTTPUpload& up = s_server.upload();
      if (up.status == UPLOAD_FILE_START) {
        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (up.status == UPLOAD_FILE_WRITE) {
        Update.write(up.buf, up.currentSize);
      } else if (up.status == UPLOAD_FILE_END) {
        Update.end(true);
      }
    });

  s_server.onNotFound([]() { s_server.send(404, "text/plain", "404"); });
}

void WebPortal::begin(RideComputer* rc) {
  _rc = rc;
  s_rc = rc;
  routes();
}

void WebPortal::startServer() {
  s_prefs.begin("wifi", true);
  String ssid = s_prefs.getString("ssid", "");
  String pass = s_prefs.getString("pass", "");
  s_prefs.end();

  _apMode = true;
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
      _apMode = false;
      strncpy(_ip, WiFi.localIP().toString().c_str(), sizeof(_ip) - 1);
    }
  }
  if (_apMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(DEVICE_NAME, "edgeesp123");
    strncpy(_ip, WiFi.softAPIP().toString().c_str(), sizeof(_ip) - 1);
  }
  // Wi-Fi and BLE share one radio; dropping TX power keeps coexistence sane.
  WiFi.setTxPower(WIFI_POWER_11dBm);
  s_server.begin();
  _running = true;
  _startedMs = millis();
}

void WebPortal::stop() {
  if (!_running) return;
  s_server.stop();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  _running = false;
  _ip[0] = 0;
}

void WebPortal::toggle() { _running ? stop() : startServer(); }

void WebPortal::handle() {
  if (!_running) return;
  s_server.handleClient();
  // Nobody rides with Wi-Fi on for an hour by choice; assume they forgot.
  if (millis() - _startedMs > 3600000UL) stop();
}
