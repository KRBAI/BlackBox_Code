#include "SimManager.h"
#include "SettingsManager.h"
#include "Sensors.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// UART2 — independent of USB-CDC which occupies UART0 on ESP32-S3
HardwareSerial sim800(2);

static SimStatus simStatus;

// ==========================================
// LOW-LEVEL HELPERS
// ==========================================

static bool atCmd(const char* cmd, const char* expect, unsigned long timeoutMs = 2000) {
  while (sim800.available()) sim800.read();
  sim800.println(cmd);
  Serial.print("[SIM] >> "); Serial.println(cmd);
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (sim800.available()) resp += (char)sim800.read();
    if (resp.indexOf(expect) >= 0) {
      Serial.print("[SIM] << "); Serial.println(resp);
      return true;
    }
  }
  Serial.println("[SIM] TIMEOUT for \"" + String(expect) + "\" | Got: " + resp);
  return false;
}

static String atQuery(const char* cmd, unsigned long timeoutMs = 1500) {
  while (sim800.available()) sim800.read();
  sim800.println(cmd);
  delay(timeoutMs);
  String resp = "";
  while (sim800.available()) resp += (char)sim800.read();
  return resp;
}

static bool waitFor(char ch, unsigned long timeoutMs = 5000) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (sim800.available() && sim800.read() == ch) return true;
  }
  Serial.println("[SIM] TIMEOUT waiting for '" + String(ch) + "'");
  return false;
}

static void parseSignal(int rssi, int& dBm, int& bars) {
  if (rssi == 99 || rssi == 0) { dBm = 0; bars = 0; return; }
  dBm = -113 + (rssi * 2);
  if      (rssi >= 20) bars = 5;
  else if (rssi >= 15) bars = 4;
  else if (rssi >= 10) bars = 3;
  else if (rssi >= 6)  bars = 2;
  else if (rssi >= 1)  bars = 1;
  else                 bars = 0;
}

// ==========================================
// RTC TIME SYNC FROM SIM NETWORK TIME
// AT+CLTS=1 — auto-sync module clock from network on registration
// AT+CCLK?  — returns "yy/MM/dd,hh:mm:ss+tz" (LKT when CLTS active)
// Time is stored in the software clock in Sensors.cpp (setSoftClock).
// ==========================================

bool syncTimeFromSIM() {
  Serial.println("[SIM] Syncing RTC from network time...");

  // Enable network time sync and save setting to SIM800L flash
  atCmd("AT+CLTS=1", "OK", 2000);
  atCmd("AT&W",      "OK", 2000);
  delay(2000); // let module latch the network time

  // Query module clock
  // Response: +CCLK: "25/03/18,14:32:05+22"  (+22 = 22 quarter-hours = UTC+5:30)
  String resp = atQuery("AT+CCLK?", 2000);
  Serial.println("[SIM] CCLK: " + resp);

  int q1 = resp.indexOf('"');
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 <= q1) {
    Serial.println("[SIM] RTC sync failed — no CCLK response.");
    return false;
  }

  String ts = resp.substring(q1 + 1, q2); // e.g. "25/03/18,14:32:05+22"
  if (ts.length() < 17) {
    Serial.println("[SIM] RTC sync failed — string too short: " + ts);
    return false;
  }

  uint8_t yr = ts.substring(0,  2).toInt();
  uint8_t mo = ts.substring(3,  5).toInt();
  uint8_t dd = ts.substring(6,  8).toInt();
  uint8_t hh = ts.substring(9,  11).toInt();
  uint8_t mn = ts.substring(12, 14).toInt();
  uint8_t ss = ts.substring(15, 17).toInt();

  if (yr == 0 && mo == 0) {
    Serial.println("[SIM] RTC sync failed — module clock not yet set by network.");
    return false;
  }

  // Convert parsed LKT time to Unix timestamp for the software clock
  // Simple conversion: days from 1970 + time-of-day seconds
  // Year is 2-digit (e.g. 26 = 2026), adjust to full year
  uint16_t fullYear = 2000 + yr;
  uint32_t days = 0;
  for (uint16_t y = 1970; y < fullYear; y++) {
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    days += leap ? 366 : 365;
  }
  static const uint8_t dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool leap = (fullYear % 4 == 0 && (fullYear % 100 != 0 || fullYear % 400 == 0));
  for (uint8_t m = 1; m < mo; m++) {
    days += dim[m-1] + (m == 2 && leap ? 1 : 0);
  }
  days += (dd - 1);
  unsigned long unixLKT = (unsigned long)days * 86400UL + hh * 3600UL + mn * 60UL + ss;
  setSoftClock(unixLKT);

  // Store readable timestamp for the SIM status page
  char buf[20];
  snprintf(buf, sizeof(buf), "20%02d-%02d-%02d %02d:%02d", yr, mo, dd, hh, mn);
  simStatus.lastRTCSync = String(buf);
  return true;
}

// ==========================================
// INIT
// ==========================================
void initSimManager() {
  sim800.begin(9600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);
  Serial.println("[SIM] UART2 RX=" + String(SIM800_RX_PIN) + " TX=" + String(SIM800_TX_PIN));
  Serial.println("[SIM] Waiting for module power-on (5s)...");
  delay(5000);

  // 1. Handshake
  bool alive = false;
  for (int i = 0; i < 5; i++) {
    if (atCmd("AT", "OK", 2000)) { alive = true; break; }
    Serial.println("[SIM] Retry " + String(i + 1) + "/5...");
    delay(1000);
  }
  if (!alive) {
    Serial.println("[SIM] ERROR: No response. Check power (needs ~2A peak) & wiring.");
    return;
  }

  // 2. Disable echo
  atCmd("ATE0", "OK");
  delay(200);

  // 3. SIM card
  if (!atCmd("AT+CPIN?", "READY", 3000)) {
    Serial.println("[SIM] ERROR: SIM not ready — not inserted, PIN locked, or damaged.");
    return;
  }
  Serial.println("[SIM] SIM card: READY");

  // 4. Initial signal quality
  String csqResp = atQuery("AT+CSQ", 1500);
  int idx = csqResp.indexOf("+CSQ: ");
  if (idx >= 0) {
    simStatus.rssi = csqResp.substring(idx + 6).toInt();
    parseSignal(simStatus.rssi, simStatus.dBm, simStatus.bars);
    Serial.println("[SIM] Signal: " + String(simStatus.rssi) +
                   " (" + String(simStatus.dBm) + " dBm, " +
                   String(simStatus.bars) + " bars)");
  }

  // 5. Network registration (up to 60s)
  Serial.println("[SIM] Waiting for network registration...");
  for (int i = 0; i < 20; i++) {
    String reg = atQuery("AT+CREG?", 1500);
    Serial.println("[SIM] CREG [" + String(i + 1) + "/20]: " + reg);
    if (reg.indexOf(",1") >= 0) { simStatus.registered = true; Serial.println("[SIM] Home network."); break; }
    if (reg.indexOf(",5") >= 0) { simStatus.registered = true; Serial.println("[SIM] Roaming."); break; }
    if (reg.indexOf(",3") >= 0) { Serial.println("[SIM] DENIED — enable 2G with operator."); break; }
    delay(2000);
  }
  if (!simStatus.registered) {
    Serial.println("[SIM] WARNING: Not registered. SMS will fail until network found.");
  }

  // 6. Operator name
  String cops = atQuery("AT+COPS?", 2000);
  int q1 = cops.indexOf('"'), q2 = cops.indexOf('"', q1 + 1);
  if (q1 >= 0 && q2 > q1) {
    simStatus.operatorName = cops.substring(q1 + 1, q2);
    Serial.println("[SIM] Operator: " + simStatus.operatorName);
  }

  // 7. Sync RTC from network time
  if (simStatus.registered) syncTimeFromSIM();

  Serial.println("[SIM] Init complete.");
}

// ==========================================
// UPDATE STATUS (call from main loop ~every 10s)
// Total AT time: ~3.5s — run infrequently
// ==========================================
void updateSimStatus() {
  // Signal strength
  String csq = atQuery("AT+CSQ", 1000);
  int idx = csq.indexOf("+CSQ: ");
  if (idx >= 0) {
    simStatus.rssi = csq.substring(idx + 6).toInt();
    parseSignal(simStatus.rssi, simStatus.dBm, simStatus.bars);
  }

  // Registration
  String reg = atQuery("AT+CREG?", 1000);
  simStatus.registered = (reg.indexOf(",1") >= 0 || reg.indexOf(",5") >= 0);

  // Operator name
  if (simStatus.registered) {
    String cops = atQuery("AT+COPS?", 1500);
    int q1 = cops.indexOf('"'), q2 = cops.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1)
      simStatus.operatorName = cops.substring(q1 + 1, q2);
  } else {
    simStatus.operatorName = "Searching...";
  }
}

SimStatus& getSimStatus() { return simStatus; }

// ==========================================
// LOCATION HELPERS
// ==========================================

// Use Google Geolocation API (WiFi-based) to get lat/lon when GPS has no fix.
// Scans visible WiFi networks and sends them to the API.
static bool getWiFiLocation(float &lat, float &lon) {
  String apiKey = settingsManager.get().googleMapsAPIKey;
  if (apiKey.length() < 10) {
    Serial.println("[LOC] No Google Maps API key configured.");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[LOC] WiFi not connected — cannot use geolocation.");
    return false;
  }

  Serial.println("[LOC] Scanning WiFi networks for geolocation...");
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("[LOC] No networks found."); return false; }

  // Build JSON body for Geolocation API
  String body = "{";
  body += "\"wifiAccessPoints\":[";
  for (int i = 0; i < min(n, 10); i++) {
    if (i > 0) body += ",";
    body += "{\"macAddress\":\"";
    body += WiFi.BSSIDstr(i);
    body += "\",\"signalStrength\":";
    body += String(WiFi.RSSI(i));
    body += "}";
  }
  body += "]}";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = "https://www.googleapis.com/geolocation/v1/geolocate?key=" + apiKey;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int code = http.POST(body);
  if (code != 200) {
    Serial.println("[LOC] Geolocation API error: " + String(code));
    http.end(); return false;
  }

  String resp = http.getString();
  http.end();
  Serial.println("[LOC] Geolocation response: " + resp);

  // Simple string parse: find "lat": and "lng":
  // Response: {"location":{"lat":6.93221,"lng":79.84178},"accuracy":150.0}
  int latIdx = resp.indexOf("\"lat\":");
  int lngIdx = resp.indexOf("\"lng\":");
  if (latIdx < 0 || lngIdx < 0) {
    Serial.println("[LOC] Could not parse geolocation response.");
    return false;
  }
  lat = resp.substring(latIdx + 7).toFloat();
  lon = resp.substring(lngIdx + 7).toFloat();
  if (lat == 0.0 && lon == 0.0) return false;
  Serial.printf("[LOC] WiFi location: %.5f, %.5f\n", lat, lon);
  return true;
}



// ==========================================
// SEND SMS
// ==========================================
static void transmitSMS(const String& phone, const String& msg) {
  if (!atCmd("AT", "OK", 3000)) {
    Serial.println("[SIM] ERROR: Module not responding."); return;
  }
  String reg = atQuery("AT+CREG?", 1500);
  if (reg.indexOf(",1") < 0 && reg.indexOf(",5") < 0) {
    Serial.println("[SIM] ERROR: Not registered."); return;
  }
  if (!atCmd("AT+CMGF=1", "OK")) return;

  String cmgsCmd = "AT+CMGS=\"" + phone + "\"";
  sim800.println(cmgsCmd);
  Serial.println("[SIM] >> " + cmgsCmd);
  if (!waitFor('>', 5000)) {
    Serial.println("[SIM] ERROR: No '>' prompt."); return;
  }

  sim800.print(msg);
  delay(200);
  sim800.write(26);
  Serial.println("[SIM] Sending...");

  String sendResp = "";
  unsigned long start = millis();
  while (millis() - start < 15000) {
    while (sim800.available()) sendResp += (char)sim800.read();
    if (sendResp.indexOf("+CMGS") >= 0) { Serial.println("[SIM] SMS sent!"); return; }
    if (sendResp.indexOf("ERROR") >= 0) { Serial.println("[SIM] Send ERROR: " + sendResp); return; }
  }
  Serial.println("[SIM] Timeout. Got: " + sendResp);
}

void sendLocationSMS(float lat, float lon) {
  Serial.println("[SIM] ── SOS SMS sequence start ──");

  String phone = settingsManager.get().phoneNumber;
  if (phone.length() < 5) {
    Serial.println("[SIM] ERROR: Phone number not configured."); return;
  }

  // ── 1. Determine location ───────────────────────────────────────
  bool gpsValid = (lat != 0.0 && lon != 0.0);
  String locationSource;

  if (!gpsValid) {
    Serial.println("[SIM] GPS not fixed — attempting WiFi geolocation...");
    if (getWiFiLocation(lat, lon)) {
      locationSource = "WiFi (approx.)";
      gpsValid = true;
    } else {
      locationSource = "Unknown";
    }
  } else {
    locationSource = "GPS";
  }

  // ── 2. Build detailed message ────────────────────────────────────
  String msg = "";
  msg += "*** BLACKBOX SOS ***\n";
  msg += "Vehicle impact detected!\n";
  msg += "\n";

  if (gpsValid) {
    // Direct location link
    msg += "LOCATION (" + locationSource + "):\n";
    msg += "https://maps.google.com/?q=";
    msg += String(lat, 5) + "," + String(lon, 5);
    msg += "\n\n";

    // Google Maps search links centred on the accident location
    // These open directly in Maps app showing nearby results
    String coordStr = String(lat, 5) + "," + String(lon, 5);

    msg += "Nearby Hospitals:\n";
    msg += "https://www.google.com/maps/search/hospital/@";
    msg += coordStr + ",15z";
    msg += "\n\n";

    msg += "Nearby Police:\n";
    msg += "https://www.google.com/maps/search/police+station/@";
    msg += coordStr + ",15z";
    msg += "\n";
  } else {
    msg += "LOCATION: Could not determine.\n";
    msg += "(No GPS fix, WiFi location unavailable)\n";
  }

  msg += "\n";
  msg += "Time: " + String(millis() / 60000) + " min uptime\n";
  msg += "Operator: " + simStatus.operatorName;

  Serial.println("[SIM] Message:\n" + msg);

  // ── 4. Transmit ──────────────────────────────────────────────────
  transmitSMS(phone, msg);
  Serial.println("[SIM] ── SOS SMS sequence end ──");
}