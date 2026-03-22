#include "WebManager.h"
#include "SettingsManager.h"
#include <HTTPClient.h>
#include <Wire.h>

float internalUsedMB  = 0;
float internalTotalMB = 0;
bool  internalMounted = false;
static unsigned long lastStorageUpdate = 0;
static unsigned long lastLogTime       = 0;

// --- WEB SERVER & FIREBASE ---
WebServer server(80);
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
FirebaseApp app;
RealtimeDatabase Database;
NoAuth no_auth;

static unsigned long lastFirebaseUpload = 0;
static bool firebaseInitialized = false;

// --- 60-SECOND AGGREGATION ---
static int    secCount     = 0;
static float  sumSpeed     = 0;
static float  maxAx = 0, maxAy = 0, maxAz = 0;
static double midLat = 0, midLon = 0;
static String startMinDate = "", startMinTime = "";

// --- FIREBASE CALLBACK ---
void asyncCallback(AsyncResult &aResult) {
  if (aResult.isError())
    Serial.printf("[Firebase] Error: %s\n", aResult.error().message().c_str());
  else if (aResult.isEvent())
    Serial.printf("[Firebase] OK: %s\n", aResult.eventLog().message().c_str());
}

// ==========================================
// INIT STORAGE
// ==========================================
void initWebManager() {
  if (FFat.begin(true)) {
    internalMounted = true;
    Serial.println("[Storage] Mounted.");
  } else {
    Serial.println("[Storage] ERROR: FFat mount failed.");
  }
}

void handleWebManager() {
  if (internalMounted && millis() - lastStorageUpdate > 2000) {
    lastStorageUpdate = millis();
    internalUsedMB  = FFat.usedBytes()  / (1024.0 * 1024.0);
    internalTotalMB = FFat.totalBytes() / (1024.0 * 1024.0);
  }
}

// ==========================================
// SETTINGS PAGE HTML
// ==========================================
static String buildSettingsPage() {
  DeviceSettings& s = settingsManager.get();
  String html = R"rawhtml(
<!DOCTYPE html><html lang="en"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>BlackBox Config</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#0d1117;color:#e6edf3;font-family:'Segoe UI',monospace;min-height:100vh}
  header{background:#161b22;border-bottom:1px solid #21262d;padding:16px 24px;display:flex;align-items:center;gap:14px}
  .logo{width:40px;height:40px;background:#e6edf3;border-radius:8px;display:flex;align-items:center;justify-content:center;font-size:20px}
  header h1{font-size:1.1rem;font-weight:700;letter-spacing:3px}
  header small{font-size:0.7rem;color:#7d8590;letter-spacing:1px;display:block;margin-top:2px}
  main{max-width:580px;margin:28px auto;padding:0 16px}
  .card{background:#161b22;border:1px solid #21262d;border-radius:12px;padding:22px;margin-bottom:20px}
  .card h2{font-size:0.75rem;text-transform:uppercase;letter-spacing:2px;color:#7d8590;margin-bottom:18px;padding-bottom:10px;border-bottom:1px solid #21262d;display:flex;align-items:center;gap:8px}
  .field{margin-bottom:14px}
  label{display:block;font-size:0.75rem;color:#8b949e;margin-bottom:5px;letter-spacing:.5px}
  .iw{position:relative}
  input[type=text],input[type=password]{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;border-radius:7px;color:#e6edf3;font-size:0.88rem;font-family:monospace}
  input:focus{outline:none;border-color:#388bfd}
  .eye{position:absolute;right:10px;top:50%;transform:translateY(-50%);background:none;border:none;color:#7d8590;cursor:pointer;font-size:13px;padding:0}
  .btn{width:100%;padding:11px;border:none;border-radius:8px;font-size:0.88rem;font-weight:700;cursor:pointer;letter-spacing:1.5px;margin-top:4px;display:flex;align-items:center;justify-content:center;gap:8px}
  .btn-green{background:#238636;color:#fff}
  .btn-green:hover{background:#2ea043}
  .btn-red{background:#b91c1c;color:#fff}
  .btn-red:hover{background:#dc2626}
  .btn-blue{background:#1d4ed8;color:#fff;margin-top:10px}
  .btn-blue:hover{background:#2563eb}
  .status{padding:10px 13px;border-radius:7px;font-size:0.82rem;margin-top:12px;display:none}
  .ok{background:#0d2d17;border:1px solid #238636;color:#3fb950;display:block}
  .err{background:#2d0d0d;border:1px solid #b91c1c;color:#f85149;display:block}
  .dc{border-color:rgba(185,28,28,.35)}.dc h2{color:#f85149}
  .warn{font-size:0.8rem;color:#7d8590;margin-bottom:14px;line-height:1.6}
  .badge{display:inline-block;padding:3px 8px;border-radius:20px;font-size:0.7rem;font-weight:600}
  .on{background:#0d2d17;color:#3fb950;border:1px solid #238636}
  .off{background:#2d0d0d;color:#f85149;border:1px solid #b91c1c}
  .time-row{display:flex;align-items:center;justify-content:space-between;background:#0d1117;border:1px solid #30363d;border-radius:7px;padding:9px 12px;font-family:monospace;font-size:0.88rem;color:#8b949e}
  .hint{font-size:0.72rem;color:#484f58;margin-top:5px}
  footer{text-align:center;color:#30363d;font-size:0.72rem;padding:20px;letter-spacing:1px}
  hr{border:none;border-top:1px solid #21262d;margin:4px 0 14px}
</style></head><body>
<header>
  <div class="logo">&#9632;</div>
  <div><h1>BLACKBOX</h1><small>DEVICE CONFIGURATION</small></div>
  <div style="margin-left:auto;font-size:0.75rem;color:#7d8590">
    Internet:&nbsp;<span class="badge )rawhtml";
  html += (WiFi.status() == WL_CONNECTED) ? "on\">ONLINE&gt;&gt;" : "off\">OFFLINE";
  html += R"rawhtml("></span>
  </div>
</header>
<main>
  <form method="POST" action="/save-settings">
    <!-- ── NETWORK ── -->
    <div class="card">
      <h2>&#128246; Network &mdash; Router</h2>
      <div class="field"><label>ROUTER SSID</label>
        <input type="text" name="routerSSID" value=")rawhtml"; html += s.routerSSID;
  html += R"rawhtml(" placeholder="WiFi name">
      </div>
      <div class="field"><label>ROUTER PASSWORD</label><div class="iw">
        <input type="password" name="routerPass" id="rp" value=")rawhtml"; html += s.routerPass;
  html += R"rawhtml(">
        <button type="button" class="eye" onclick="tog('rp')">&#128065;</button>
      </div></div>
    </div>

    <!-- ── EMERGENCY CONTACT ── -->
    <div class="card">
      <h2>&#128241; Emergency Contact</h2>
      <div class="field"><label>PHONE NUMBER (with country code)</label>
        <input type="text" name="phoneNumber" value=")rawhtml"; html += s.phoneNumber;
  html += R"rawhtml(" placeholder="+94xxxxxxxxx">
      </div>

    </div>

    <!-- ── FIREBASE ── -->
    <div class="card">
      <h2>&#128293; Firebase</h2>
      <div class="field"><label>DATABASE URL</label>
        <input type="text" name="firebaseURL" value=")rawhtml"; html += s.firebaseURL;
  html += R"rawhtml(">
      </div>
      <div class="field"><label>API KEY</label><div class="iw">
        <input type="password" name="firebaseAPIKey" id="fak" value=")rawhtml"; html += s.firebaseAPIKey;
  html += R"rawhtml(">
        <button type="button" class="eye" onclick="tog('fak')">&#128065;</button>
      </div></div>
      <div class="field"><label>USER ID</label>
        <input type="text" name="userID" value=")rawhtml"; html += s.userID;
  html += R"rawhtml(" placeholder="Firebase UID">
      </div>
    </div>

    <!-- ── GOOGLE MAPS ── -->
    <div class="card">
      <h2>&#128205; Google Maps API Key</h2>
      <div class="field"><label>API KEY (enables WiFi geolocation when GPS has no fix)</label>
        <div class="iw">
          <input type="password" name="googleMapsAPIKey" id="gmk" value=")rawhtml"; html += s.googleMapsAPIKey;
  html += R"rawhtml(" placeholder="AIzaSy...">
          <button type="button" class="eye" onclick="tog('gmk')">&#128065;</button>
        </div>
      </div>
    </div>

      <p class="hint">&#9432; The device RTC will be synced to this time when you click Save &amp; Reboot.</p>
    </div>

    <button type="submit" class="btn btn-green">&#128190;&nbsp; SAVE &amp; REBOOT</button>
  </form>

    <div id="sync-status" class="status"></div>
  </div>

  <!-- ── DANGER ZONE ── -->
  <div class="card dc">
    <h2>&#9888; Danger Zone</h2>
    <p class="warn">Permanently deletes all Trip History from Firebase. Local flash is not affected.</p>
    <button type="button" class="btn btn-red" onclick="clearFB()">&#128465;&nbsp; CLEAR ALL FIREBASE TELEMETRY</button>
    <div id="cs" class="status"></div>
  </div>
</main>
<footer>BLACKBOX S3 &nbsp;&#183;&nbsp; )rawhtml";
  html += String(internalUsedMB, 1) + " MB / " + String(internalTotalMB, 1) + " MB";
  html += R"rawhtml(</footer>
<script>
  // --- Toggle password visibility ---
  function tog(id) {
    const e = document.getElementById(id);
    e.type = e.type === 'password' ? 'text' : 'password';
  }

  // --- Clear Firebase ---
  function clearFB() {
    if (!confirm('Delete ALL Trip History from Firebase?\nThis cannot be undone.')) return;
    const s = document.getElementById('cs');
    s.className = 'status'; s.style.display = 'block'; s.textContent = 'Deleting...';
    fetch('/clear-firebase', {method:'POST'})
      .then(r => r.text())
      .then(t => { s.className='status '+(t.startsWith('OK')?'ok':'err'); s.textContent=t; })
      .catch(() => { s.className='status err'; s.textContent='Request failed.'; });
  }
</script></body></html>
)rawhtml";
  return html;
}

// ==========================================
// WEB SERVER SETUP
// ==========================================
void initWebServer() {
  DeviceSettings& s = settingsManager.get();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  WiFi.setAutoReconnect(true);
  WiFi.begin(s.routerSSID.c_str(), s.routerPass.c_str());

  // --- Root status page ---
  server.on("/", HTTP_GET, []() {
    String html = "<html><body style='font-family:Arial;text-align:center;color:white;background:#0d1117;padding:40px'>";
    html += "<h2 style='letter-spacing:3px'>BLACKBOX STATUS</h2><br>";
    html += "<p>Storage: " + String(internalUsedMB, 2) + " MB / " + String(internalTotalMB, 2) + " MB</p>";
    html += "<p>WiFi: " + String(WiFi.status() == WL_CONNECTED
              ? "Connected (" + WiFi.localIP().toString() + ")"
              : "Offline") + "</p>";
    html += "<br><a href='/settings' style='color:#388bfd;font-size:1.1rem'>&#9881; Open Settings</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  // --- Settings page ---
  server.on("/settings", HTTP_GET, []() {
    server.send(200, "text/html", buildSettingsPage());
  });

  // --- Save settings + sync RTC ---
  server.on("/save-settings", HTTP_POST, []() {
    DeviceSettings n;
    n.routerSSID       = server.arg("routerSSID");
    n.routerPass       = server.arg("routerPass");
    n.phoneNumber      = server.arg("phoneNumber");
    n.firebaseURL      = server.arg("firebaseURL");
    n.firebaseAPIKey   = server.arg("firebaseAPIKey");
    n.userID           = server.arg("userID");
    n.googleMapsAPIKey = server.arg("googleMapsAPIKey");
    settingsManager.save(n);


    String html = "<html><body style='font-family:Arial;text-align:center;color:white;background:#0d1117;padding:60px'>";
    html += "<h2 style='color:#3fb950'>&#10003; SAVED!</h2><br>";
    html += "<p style='color:#8b949e'>Rebooting in 2 seconds...</p>";
    html += "<script>setTimeout(()=>{window.location.href='/settings'},4000)</script>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    delay(1500);
    ESP.restart();
  });



  // --- Clear Firebase ---
  server.on("/clear-firebase", HTTP_POST, []() {
    if (WiFi.status() != WL_CONNECTED) {
      server.send(200, "text/plain", "ERROR: Not connected to internet.");
      return;
    }
    DeviceSettings& s = settingsManager.get();
    String url = "https://" + s.firebaseURL + "users/" + s.userID + "/Trip_History.json?auth=" + s.firebaseAPIKey;
    url.replace("//users", "/users");
    WiFiClientSecure c; c.setInsecure();
    HTTPClient http; http.begin(c, url);
    int code = http.sendRequest("DELETE");
    http.end();
    if (code == 200) server.send(200, "text/plain", "OK — Trip History deleted.");
    else server.send(200, "text/plain", "ERROR: Firebase returned HTTP " + String(code));
  });

  server.begin();
  Serial.println("[WebServer] Started. Connect to " + String(WIFI_SSID) + " -> http://192.168.4.1/settings");
}

void handleWebServer() { server.handleClient(); }

// ==========================================
// 60-SECOND DATA AGGREGATION & STORAGE
// ==========================================
void logDataToInternal(SensorData d) {
  if (!internalMounted || millis() - lastLogTime < 1000) return;
  lastLogTime = millis();

  if (secCount == 0) {
    startMinDate = d.dateStr; startMinTime = d.timeStr;
    midLat = d.lat; midLon = d.lon;
  }
  if (secCount == 30) { midLat = d.lat; midLon = d.lon; }

  sumSpeed += d.speed;
  if (abs(d.ax) > maxAx) maxAx = abs(d.ax);
  if (abs(d.ay) > maxAy) maxAy = abs(d.ay);
  if (abs(d.az) > maxAz) maxAz = abs(d.az);
  secCount++;

  if (secCount >= 60) {
    float avgSpeed = sumSpeed / 60.0;
    String csvLine = startMinDate + "," + startMinTime + "," +
                     String(midLat, 5) + "," + String(midLon, 5) + "," +
                     String(avgSpeed, 1) + "," + String(maxAx, 2) + "," +
                     String(maxAy, 2) + "," + String(maxAz, 2) + "\n";
    File file = FFat.open("/offline_queue.txt", FILE_APPEND);
    if (file) { file.print(csvLine); file.close(); }
    Serial.println(">>> 60s block saved to flash <<<");
    secCount = 0; sumSpeed = 0; maxAx = 0; maxAy = 0; maxAz = 0;
  }
}

// ==========================================
// FORCE SAVE — flush partial buffer to flash
// ==========================================
void forceSaveData() {
  if (!internalMounted || secCount == 0) return;

  float avgSpeed = sumSpeed / (float)secCount;
  String csvLine = startMinDate + "," + startMinTime + "," +
                   String(midLat, 5) + "," + String(midLon, 5) + "," +
                   String(avgSpeed, 1) + "," + String(maxAx, 2) + "," +
                   String(maxAy, 2) + "," + String(maxAz, 2) + "\n";
  File file = FFat.open("/offline_queue.txt", FILE_APPEND);
  if (file) { file.print(csvLine); file.close(); }
  Serial.println(">>> forceSaveData: partial buffer flushed <<<");
  secCount = 0; sumSpeed = 0; maxAx = 0; maxAy = 0; maxAz = 0;
}

// ==========================================
// UPLOAD TO FIREBASE
// ==========================================
void uploadToFirebase(SensorData d) {
  if (WiFi.status() == WL_CONNECTED) {
    if (!firebaseInitialized) {
      Serial.println("[Firebase] Initializing...");
      ssl_client.setInsecure();
      initializeApp(aClient, app, getAuth(no_auth));
      app.getApp<RealtimeDatabase>(Database);
      Database.url(settingsManager.get().firebaseURL.c_str());
      firebaseInitialized = true;
    }

    app.loop();

    if (app.ready() && FFat.exists("/offline_queue.txt") && millis() - lastFirebaseUpload > 3000) {
      lastFirebaseUpload = millis();
      Serial.println("[Firebase] Uploading queue...");
      FFat.rename("/offline_queue.txt", "/uploading.txt");
      File file = FFat.open("/uploading.txt", FILE_READ);
      if (file) {
        while (file.available()) {
          String payload = file.readStringUntil('\n');
          payload.trim();
          if (payload.length() > 10) {
            int p1=payload.indexOf(','), p2=payload.indexOf(',',p1+1),
                p3=payload.indexOf(',',p2+1), p4=payload.indexOf(',',p3+1),
                p5=payload.indexOf(',',p4+1), p6=payload.indexOf(',',p5+1),
                p7=payload.indexOf(',',p6+1);
            if (p7 > 0) {
              String dDate=payload.substring(0,p1), dTime=payload.substring(p1+1,p2);
              double dLat=payload.substring(p2+1,p3).toDouble(), dLon=payload.substring(p3+1,p4).toDouble();
              float dSpd=payload.substring(p4+1,p5).toFloat(),
                    dAx=payload.substring(p5+1,p6).toFloat(),
                    dAy=payload.substring(p6+1,p7).toFloat(),
                    dAz=payload.substring(p7+1).toFloat();
              JsonWriter writer;
              object_t json, d_obj, t_obj, lat_obj, lon_obj, spd_obj, ax_obj, ay_obj, az_obj;
              writer.create(d_obj,"date",dDate); writer.create(t_obj,"time",dTime);
              writer.create(lat_obj,"latitude",number_t(dLat,5));
              writer.create(lon_obj,"longitude",number_t(dLon,5));
              writer.create(spd_obj,"avg_speed_kmh",number_t(dSpd,1));
              writer.create(ax_obj,"max_gForce_X",number_t(dAx,2));
              writer.create(ay_obj,"max_gForce_Y",number_t(dAy,2));
              writer.create(az_obj,"max_gForce_Z",number_t(dAz,2));
              writer.join(json,8,d_obj,t_obj,lat_obj,lon_obj,spd_obj,ax_obj,ay_obj,az_obj);
              String path = "/users/" + settingsManager.get().userID + "/Trip_History";
              Database.push<object_t>(aClient, path, json, asyncCallback);
              app.loop();
              delay(50);
            }
          }
        }
        file.close();
        FFat.remove("/uploading.txt");
        Serial.println("[Firebase] Upload complete.");
      }
    }
  } else {
    firebaseInitialized = false;
  }
}