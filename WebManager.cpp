#include "WebManager.h"
#include <HTTPClient.h>
#include <Wire.h>

float internalUsedMB  = 0;
float internalTotalMB = 0;
bool  internalMounted = false;
static unsigned long lastStorageUpdate = 0;
static unsigned long lastLogTime       = 0;

// Web-injected location
double webInjectedLat      = 0.0;
double webInjectedLon      = 0.0;
float  webInjectedSpeed    = 0.0;
bool   webLocationInjected = false;

// Web-injected blindspot distances (cm).  -1 = no reading yet.
int webBlindLeft  = -1;
int webBlindRight = -1;

// --- WEB SERVER & FIREBASE ---
WebServer server(80);
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
FirebaseApp app;
RealtimeDatabase Database;
NoAuth no_auth;

static unsigned long lastFirebaseUpload = 0;
static unsigned long lastLiveUpload     = 0;
static bool firebaseInitialized = false;

// --- 60-SECOND AGGREGATION ---
static int    secCount     = 0;
static float  sumSpeed     = 0;
static float  maxAx = 0, maxAy = 0, maxAz = 0;
static double midLat = 0, midLon = 0;
static String startMinDate = "", startMinTime = "";

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
    internalUsedMB  = FFat.usedBytes()  / (1024.0f * 1024.0f);
    internalTotalMB = FFat.totalBytes() / (1024.0f * 1024.0f);
  }
}

// ==========================================
// WEB SERVER SETUP
// ==========================================
void initWebServer() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ROUTER_SSID, ROUTER_PASS);
  Serial.println("[WiFi] Connecting to " + String(ROUTER_SSID) + "...");

  // --- Root status page ---
  server.on("/", HTTP_GET, []() {
    String html = "<html><body style='font-family:Arial;text-align:center;"
                  "color:white;background:#0d1117;padding:40px'>";
    html += "<h2 style='letter-spacing:3px'>BLACKBOX STATUS</h2><br>";
    html += "<p>WiFi: " + String(WiFi.status() == WL_CONNECTED
              ? "Connected (" + WiFi.localIP().toString() + ")"
              : "Connecting...") + "</p>";
    html += "<p>GPRS: " + String(gprsConnected() ? "Connected" : "Offline") + "</p>";
    html += "<p>Storage: " + String(internalUsedMB, 1) + " / "
                           + String(internalTotalMB, 1) + " MB</p>";
    html += "<p>Blindspot L: " + String(webBlindLeft)  + " cm</p>";
    html += "<p>Blindspot R: " + String(webBlindRight) + " cm</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  // --- Push location from webapp (GPS fallback) ---
  // POST body: {"lat":6.93221,"lon":79.84178,"speed":42.5}
  server.on("/push-location", HTTP_OPTIONS, []() {
    server.sendHeader("Access-Control-Allow-Origin",  "*");
    server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
    server.send(204);
  });
  server.on("/push-location", HTTP_POST, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    String body = server.arg("plain");
    auto extractFloat = [&](const String& key) -> float {
      int idx = body.indexOf("\"" + key + "\":");
      if (idx < 0) return 0.0f;
      return body.substring(idx + key.length() + 3).toFloat();
    };
    float lat   = extractFloat("lat");
    float lon   = extractFloat("lon");
    float speed = extractFloat("speed");
    if (lat != 0.0f || lon != 0.0f) {
      webInjectedLat      = lat;
      webInjectedLon      = lon;
      webInjectedSpeed    = speed;
      webLocationInjected = true;
      Serial.printf("[WebLoc] %.5f, %.5f  spd=%.1f\n", lat, lon, speed);
      server.send(200, "application/json", "{\"ok\":true}");
    } else {
      server.send(400, "application/json", "{\"ok\":false}");
    }
  });
  server.begin();
  Serial.println("[WebServer] Started.");
}

void handleWebServer() { server.handleClient(); }

// ==========================================
// 60-SECOND DATA AGGREGATION & STORAGE
// ==========================================
void logDataToInternal(const SensorData& d) {
  if (!internalMounted || millis() - lastLogTime < 1000) return;
  lastLogTime = millis();

  double logLat = d.gpsValid ? d.lat : (d.webLocationValid ? d.webLat : 0.0);
  double logLon = d.gpsValid ? d.lon : (d.webLocationValid ? d.webLon : 0.0);

  if (secCount == 0) {
    startMinDate = d.dateStr; startMinTime = d.timeStr;
    midLat = logLat; midLon = logLon;
  }
  if (secCount == 30) { midLat = logLat; midLon = logLon; }

  sumSpeed += d.speed;
  if (fabsf(d.ax) > maxAx) maxAx = fabsf(d.ax);
  if (fabsf(d.ay) > maxAy) maxAy = fabsf(d.ay);
  if (fabsf(d.az) > maxAz) maxAz = fabsf(d.az);
  secCount++;

  if (secCount >= 60) {
    float avgSpeed = sumSpeed / 60.0f;
    String csvLine = startMinDate + "," + startMinTime + "," +
                     String(midLat, 5) + "," + String(midLon, 5) + "," +
                     String(avgSpeed, 1) + "," + String(maxAx, 2) + "," +
                     String(maxAy, 2) + "," + String(maxAz, 2) + "\n";
    File file = FFat.open("/offline_queue.txt", FILE_APPEND);
    if (file) { file.print(csvLine); file.close(); }
    Serial.println(">>> 60s block saved <<<");
    secCount = 0; sumSpeed = 0; maxAx = 0; maxAy = 0; maxAz = 0;
  }
}

void forceSaveData() {
  if (!internalMounted || secCount == 0) return;
  float avgSpeed = sumSpeed / (float)secCount;
  String csvLine = startMinDate + "," + startMinTime + "," +
                   String(midLat, 5) + "," + String(midLon, 5) + "," +
                   String(avgSpeed, 1) + "," + String(maxAx, 2) + "," +
                   String(maxAy, 2) + "," + String(maxAz, 2) + "\n";
  File file = FFat.open("/offline_queue.txt", FILE_APPEND);
  if (file) { file.print(csvLine); file.close(); }
  Serial.println(">>> forceSaveData flushed <<<");
  secCount = 0; sumSpeed = 0; maxAx = 0; maxAy = 0; maxAz = 0;
}

// ==========================================
// FIREBASE UPLOAD
// ==========================================
static bool uploadLineGPRS(const String& payload) {
  int p1=payload.indexOf(','), p2=payload.indexOf(',',p1+1),
      p3=payload.indexOf(',',p2+1), p4=payload.indexOf(',',p3+1),
      p5=payload.indexOf(',',p4+1), p6=payload.indexOf(',',p5+1),
      p7=payload.indexOf(',',p6+1);
  if (p7 < 0) return false;

  String dDate=payload.substring(0,p1),      dTime=payload.substring(p1+1,p2);
  String dLat =payload.substring(p2+1,p3),   dLon =payload.substring(p3+1,p4);
  String dSpd =payload.substring(p4+1,p5),   dAx  =payload.substring(p5+1,p6);
  String dAy  =payload.substring(p6+1,p7),   dAz  =payload.substring(p7+1);

  String body = "{\"date\":\""+dDate+"\",\"time\":\""+dTime+"\"," +
                "\"latitude\":"+dLat+",\"longitude\":"+dLon+"," +
                "\"avg_speed_kmh\":"+dSpd+"," +
                "\"max_gForce_X\":"+dAx+",\"max_gForce_Y\":"+dAy+",\"max_gForce_Z\":"+dAz+"}";

  String host = String(FIREBASE_URL);
  if (host.startsWith("https://")) host = host.substring(8);
  if (host.endsWith("/"))          host = host.substring(0, host.length()-1);
  String path = "/users/" + String(USER_ID) + "/Trip_History.json?auth=" + String(FIREBASE_API_KEY);

  return (httpPostGPRS(host, path, body) == 200);
}

// Persistent TLS client — avoids a fresh handshake on every poll call.
// Declared static so it lives for the lifetime of the program.
static WiFiClientSecure gpsClient;
static bool gpsClientReady = false;

void pollTelemetryLocation() {
  String host = String(FIREBASE_URL);
  if (host.startsWith("https://")) host = host.substring(8);
  if (host.startsWith("http://"))  host = host.substring(7);
  if (host.endsWith("/"))          host = host.substring(0, host.length()-1);

  String url = "https://" + host + "/telemetry/gps.json?auth=" + String(FIREBASE_API_KEY);

  if (!gpsClientReady) { gpsClient.setInsecure(); gpsClientReady = true; }
  HTTPClient http;
  http.begin(gpsClient, url);
  http.setTimeout(3000);
  int code = http.GET();
  if (code != 200) { http.end(); return; }

  String body = http.getString();
  http.end();

  auto extractDouble = [&](const String& key) -> double {
    String search = "\"" + key + "\":";
    int idx = body.indexOf(search);
    if (idx < 0) return 0.0;
    int start = idx + search.length(), end = start;
    while (end < (int)body.length()) {
      char c = body[end];
      if (c == ',' || c == '}' || c == ' ' || c == '\n') break;
      end++;
    }
    return body.substring(start, end).toDouble();
  };

  double lat  = extractDouble("latitude");
  double lon  = extractDouble("longitude");
  float  spd  = (float)extractDouble("speed_kmh");
  if (lat == 0.0 && lon == 0.0) return;

  webInjectedLat      = lat;
  webInjectedLon      = lon;
  webInjectedSpeed    = spd;
  webLocationInjected = true;
  Serial.printf("[TelePoll] %.6f, %.6f  spd=%.1f\n", lat, lon, spd);
}

void uploadToFirebase(const SensorData& d) {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);
  bool gprsOk = gprsConnected();

  double useLat   = d.gpsValid ? d.lat   : (d.webLocationValid ? d.webLat   : 0.0);
  double useLon   = d.gpsValid ? d.lon   : (d.webLocationValid ? d.webLon   : 0.0);
  float  useSpeed = d.gpsValid ? d.speed : (d.webLocationValid ? d.webSpeed : 0.0f);
  String locSrc   = d.gpsValid ? "GPS"   : (d.webLocationValid ? "WEB"      : "NONE");

  // ── Path A: WiFi → FirebaseClient library ────────────────────────
  if (wifiOk) {
    if (!firebaseInitialized) {
      ssl_client.setInsecure();
      initializeApp(aClient, app, getAuth(no_auth));
      app.getApp<RealtimeDatabase>(Database);
      Database.url(FIREBASE_URL);
      firebaseInitialized = true;
    }

    app.loop();
    if (!app.ready()) return;

    // 1-second live push to /users/<uid>/Live
    if (millis() - lastLiveUpload >= 1000) {
      lastLiveUpload = millis();
      JsonWriter writer;
      object_t liveJson, fLat, fLon, fSpd, fAx, fAy, fAz, fTmp, fSrc, fDate, fTime;
      writer.create(fDate, "date",         d.dateStr);
      writer.create(fTime, "time",         d.timeStr);
      writer.create(fLat,  "latitude",     number_t(useLat,   5));
      writer.create(fLon,  "longitude",    number_t(useLon,   5));
      writer.create(fSpd,  "speed_kmh",    number_t(useSpeed, 1));
      writer.create(fAx,   "ax",           number_t(d.ax,     3));
      writer.create(fAy,   "ay",           number_t(d.ay,     3));
      writer.create(fAz,   "az",           number_t(d.az,     3));
      writer.create(fTmp,  "temp_c",       number_t(d.temp,   1));
      writer.create(fSrc,  "location_src", locSrc);
      writer.join(liveJson, 10, fDate, fTime, fLat, fLon, fSpd, fAx, fAy, fAz, fTmp, fSrc);
      String livePath = "/users/" + String(USER_ID) + "/Live";
      Database.set<object_t>(aClient, livePath, liveJson, asyncCallback);
      app.loop();
    }

    // Trip history queue drain (every 3 s)
    if (FFat.exists("/offline_queue.txt") && millis() - lastFirebaseUpload > 3000) {
      lastFirebaseUpload = millis();
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
              float  dSpd=payload.substring(p4+1,p5).toFloat(),
                     dAx =payload.substring(p5+1,p6).toFloat(),
                     dAy =payload.substring(p6+1,p7).toFloat(),
                     dAz =payload.substring(p7+1).toFloat();
              JsonWriter w2;
              object_t json,d_o,t_o,la_o,lo_o,sp_o,ax_o,ay_o,az_o;
              w2.create(d_o,"date",dDate);          w2.create(t_o,"time",dTime);
              w2.create(la_o,"latitude",number_t(dLat,5));
              w2.create(lo_o,"longitude",number_t(dLon,5));
              w2.create(sp_o,"avg_speed_kmh",number_t(dSpd,1));
              w2.create(ax_o,"max_gForce_X",number_t(dAx,2));
              w2.create(ay_o,"max_gForce_Y",number_t(dAy,2));
              w2.create(az_o,"max_gForce_Z",number_t(dAz,2));
              w2.join(json,8,d_o,t_o,la_o,lo_o,sp_o,ax_o,ay_o,az_o);
              String path = "/users/" + String(USER_ID) + "/Trip_History";
              Database.push<object_t>(aClient, path, json, asyncCallback);
              app.loop();
              vTaskDelay(pdMS_TO_TICKS(10)); // yield to FreeRTOS, don't block loop
            }
          }
        }
        file.close();
        FFat.remove("/uploading.txt");
      }
    }
    return;
  }

  firebaseInitialized = false;

  // ── Path B: GPRS → raw HTTP POST ─────────────────────────────────
  if (!gprsOk || !FFat.exists("/offline_queue.txt")) return;
  if (millis() - lastFirebaseUpload < 15000) return;
  lastFirebaseUpload = millis();

  FFat.rename("/offline_queue.txt", "/uploading.txt");
  File file = FFat.open("/uploading.txt", FILE_READ);
  if (!file) return;

  while (file.available()) {
    String payload = file.readStringUntil('\n');
    payload.trim();
    if (payload.length() <= 10) continue;
    if (!uploadLineGPRS(payload)) {
      File retry = FFat.open("/offline_queue.txt", FILE_APPEND);
      if (retry) { retry.println(payload); retry.close(); }
    }
    delay(200);
  }
  file.close();
  FFat.remove("/uploading.txt");
}

// ==========================================
// BLINDSPOT POLL (reads from ESP32-C3 node)
// ==========================================
// The ESP32-C3 writes to /telemetry every 40 ms:
//   { "left": 45, "right": 120,
//     "hazard_left": true, "hazard_right": false }
// We use the hazard_* booleans (threshold already applied by the C3)
// and store the raw distances for the OLED display.
// Called from telemetryPollTask every 500 ms — fast enough to feel
// real-time, slow enough not to thrash the Firebase REST endpoint.
static WiFiClientSecure bsClient;
static bool bsClientReady = false;

void pollBlindspotData() {
  if (WiFi.status() != WL_CONNECTED) return;

  String host = String(FIREBASE_URL);
  if (host.startsWith("https://")) host = host.substring(8);
  if (host.startsWith("http://"))  host = host.substring(7);
  if (host.endsWith("/"))          host = host.substring(0, host.length() - 1);

  String url = "https://" + host + "/telemetry.json?auth=" + String(FIREBASE_API_KEY);

  if (!bsClientReady) { bsClient.setInsecure(); bsClientReady = true; }
  HTTPClient http;
  http.begin(bsClient, url);
  http.setTimeout(2000);
  int code = http.GET();
  if (code != 200) { http.end(); return; }

  String body = http.getString();
  http.end();

  // Generic integer extractor
  auto extractInt = [&](const String& key) -> int {
    String search = "\"" + key + "\":";
    int idx = body.indexOf(search);
    if (idx < 0) return -1;
    int start = idx + search.length();
    // skip whitespace
    while (start < (int)body.length() && body[start] == ' ') start++;
    int end = start;
    while (end < (int)body.length()) {
      char c = body[end];
      if (c == ',' || c == '}' || c == ' ' || c == '\n') break;
      end++;
    }
    return body.substring(start, end).toInt();
  };

  // Generic bool extractor (looks for "true" / "false")
  auto extractBool = [&](const String& key) -> bool {
    String search = "\"" + key + "\":";
    int idx = body.indexOf(search);
    if (idx < 0) return false;
    int start = idx + search.length();
    while (start < (int)body.length() && body[start] == ' ') start++;
    return body.substring(start, start + 4) == "true";
  };

  int  rawLeft  = extractInt("left");
  int  rawRight = extractInt("right");
  bool hazLeft  = extractBool("hazard_left");
  bool hazRight = extractBool("hazard_right");

  // Use the C3's own hazard flag as the trigger; store raw cm for the display.
  // If the node is unreachable / returns stale data, -1 keeps hazard inactive.
  webBlindLeft  = hazLeft  ? rawLeft  : -1;
  webBlindRight = hazRight ? rawRight : -1;

  Serial.printf("[Blindspot] L=%d cm (%s)  R=%d cm (%s)\n",
                rawLeft,  hazLeft  ? "HAZ" : "ok",
                rawRight, hazRight ? "HAZ" : "ok");
}