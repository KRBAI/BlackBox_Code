#include "SimManager.h"
#include "SettingsManager.h"
#include "Sensors.h"     // softClockBase, softClockSetAt, setSoftClock()
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// UART2 — independent of USB-CDC which occupies UART0 on ESP32-S3
HardwareSerial sim800(2);

static SimStatus simStatus;

// Mutex protecting simStatus — written on Core 0 (simStatusTask / initSimManager),
// read on Core 1 (main loop → drawSimPage / sendLocationSMS).
SemaphoreHandle_t simStatusMutex = nullptr;

// ==========================================
// LOW-LEVEL HELPERS
// ==========================================

static constexpr uint8_t SMS_CTRL_Z   = 26;    // ASCII CTRL-Z — terminates AT+CMGS message body
static constexpr int     DBM_BASE     = -113;  // SIM800L: dBm = DBM_BASE + (rssi * 2)
static constexpr int     DBM_STEP     =  2;    // dBm per RSSI unit

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
  dBm = DBM_BASE + (rssi * DBM_STEP);
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
  // LKT = UTC+5:30.  AT+CLTS gives local (LKT) time; convert to UTC before
  // storing in the software clock, which always keeps UTC internally.
  static const unsigned long LKT_OFFSET_SEC = 19800UL; // 5h30m in seconds

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
  unsigned long unixUTC = unixLKT - LKT_OFFSET_SEC; // store as UTC
  setSoftClock(unixUTC);

  // Store readable LKT timestamp for the SIM status page (shown to user as local time)
  char buf[24];
  snprintf(buf, sizeof(buf), "20%02d-%02d-%02d %02d:%02d LKT", yr, mo, dd, hh, mn);
  simStatus.lastRTCSync = String(buf);
  return true;
}

// ==========================================
// INIT
// ==========================================
void initSimManager() {
  simStatusMutex = xSemaphoreCreateMutex();

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
    xSemaphoreTake(simStatusMutex, portMAX_DELAY);
    simStatus.rssi = csqResp.substring(idx + 6).toInt();
    parseSignal(simStatus.rssi, simStatus.dBm, simStatus.bars);
    xSemaphoreGive(simStatusMutex);
    Serial.println("[SIM] Signal: " + String(simStatus.rssi) +
                   " (" + String(simStatus.dBm) + " dBm, " +
                   String(simStatus.bars) + " bars)");
  }

  // 5. Network registration (up to 60s)
  Serial.println("[SIM] Waiting for network registration...");
  for (int i = 0; i < 20; i++) {
    String reg = atQuery("AT+CREG?", 1500);
    Serial.println("[SIM] CREG [" + String(i + 1) + "/20]: " + reg);
    if (reg.indexOf(",1") >= 0) {
      xSemaphoreTake(simStatusMutex, portMAX_DELAY);
      simStatus.registered = true;
      xSemaphoreGive(simStatusMutex);
      Serial.println("[SIM] Home network."); break;
    }
    if (reg.indexOf(",5") >= 0) {
      xSemaphoreTake(simStatusMutex, portMAX_DELAY);
      simStatus.registered = true;
      xSemaphoreGive(simStatusMutex);
      Serial.println("[SIM] Roaming."); break;
    }
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
    xSemaphoreTake(simStatusMutex, portMAX_DELAY);
    simStatus.operatorName = cops.substring(q1 + 1, q2);
    xSemaphoreGive(simStatusMutex);
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
    int rssi = csq.substring(idx + 6).toInt();
    int dBm, bars;
    parseSignal(rssi, dBm, bars);
    xSemaphoreTake(simStatusMutex, portMAX_DELAY);
    simStatus.rssi = rssi;
    simStatus.dBm  = dBm;
    simStatus.bars = bars;
    xSemaphoreGive(simStatusMutex);
  }

  // Registration
  String reg = atQuery("AT+CREG?", 1000);
  bool registered = (reg.indexOf(",1") >= 0 || reg.indexOf(",5") >= 0);
  xSemaphoreTake(simStatusMutex, portMAX_DELAY);
  simStatus.registered = registered;
  xSemaphoreGive(simStatusMutex);

  // Operator name
  if (registered) {
    String cops = atQuery("AT+COPS?", 1500);
    int q1 = cops.indexOf('"'), q2 = cops.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) {
      String op = cops.substring(q1 + 1, q2);
      xSemaphoreTake(simStatusMutex, portMAX_DELAY);
      simStatus.operatorName = op;
      xSemaphoreGive(simStatusMutex);
    }
  } else {
    xSemaphoreTake(simStatusMutex, portMAX_DELAY);
    simStatus.operatorName = "Searching...";
    xSemaphoreGive(simStatusMutex);
  }
}

// Returns a snapshot copy of simStatus under the mutex so the caller
// can read all fields without holding the lock during display rendering.
SimStatus getSimStatus() {
  xSemaphoreTake(simStatusMutex, portMAX_DELAY);
  SimStatus snap = simStatus;
  xSemaphoreGive(simStatusMutex);
  return snap;
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
  sim800.write(SMS_CTRL_Z);
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

void sendLocationSMS(float lat, float lon, SosEventType eventType) {
  Serial.println("[SIM] ── SOS SMS sequence start ──");

  String phone = settingsManager.get().phoneNumber;
  if (phone.length() < 5) {
    Serial.println("[SIM] ERROR: Phone number not configured."); return;
  }

  // ── 1. Determine location ───────────────────────────────────────
  bool gpsValid = (lat != 0.0 && lon != 0.0);
  String locationSource;

  if (!gpsValid) {
    // GPS has no fix — check if the webapp pushed a location via /push-location
    if (webLocationInjected && (webInjectedLat != 0.0 || webInjectedLon != 0.0)) {
      lat = (float)webInjectedLat;
      lon = (float)webInjectedLon;
      locationSource = "Device (web fallback)";
      gpsValid = true;
      Serial.printf("[SIM] Using web-injected location: %.5f, %.5f\n", lat, lon);
    } else {
      locationSource = "Unknown";
      Serial.println("[SIM] No GPS fix and no web location available.");
    }
  } else {
    locationSource = "GPS";
  }

  // ── 2. Build event-specific header ──────────────────────────────
  String msg = "";
  msg += "*** BLACKBOX SOS ***\n";
  switch (eventType) {
    case SOS_ROLLOVER:
      msg += "ROLLOVER DETECTED!\n";
      msg += "(Vehicle sustained tilt beyond " + String((int)ROLLOVER_ANGLE_DEG) + " degrees)\n";
      break;
    case SOS_MANUAL:
      msg += "MANUAL SOS TRIGGERED!\n";
      msg += "(Driver pressed the SOS button)\n";
      break;
    case SOS_IMPACT:
    default:
      msg += "Vehicle impact detected!\n";
      break;
  }
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

  // Build UTC time string from software clock for the SOS message
  {
    static constexpr unsigned long LKT_OFFSET_SEC = 19800UL;
    unsigned long elapsed = (millis() - softClockSetAt) / 1000UL;
    unsigned long t = softClockBase + elapsed;
    // Convert to LKT for the SMS (recipients are in Sri Lanka)
    t += LKT_OFFSET_SEC;
    uint8_t hh = (t / 3600) % 24;
    uint8_t mn = (t / 60)   % 60;
    uint8_t ss = t % 60;
    char tbuf[12];
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", hh, mn, ss);
    msg += "Time (LKT): " + String(tbuf) + "\n";
  }
  msg += "Operator: " + simStatus.operatorName;

  Serial.println("[SIM] Message:\n" + msg);

  // ── 4. Transmit ──────────────────────────────────────────────────
  transmitSMS(phone, msg);
  Serial.println("[SIM] ── SOS SMS sequence end ──");
}
// ==========================================
// GPRS — CELLULAR DATA
// ==========================================
//
// How SIM800L GPRS works at the AT command level:
//
//  1. AT+SAPBR  — configure and open a "bearer" (PDP context) with the
//                 operator's APN.  This is the GPRS equivalent of connecting
//                 to a WiFi network.  The module is assigned an IP address.
//
//  2. AT+CIPSTART — open a raw TCP connection to a remote host/port.
//                   We use port 443 for HTTPS, but SIM800L does not do TLS
//                   natively in standard firmware.  For Firebase REST calls
//                   we use port 80 and HTTP (not HTTPS).  The data in transit
//                   is unencrypted, which is acceptable for telemetry that
//                   contains no personal authentication tokens — we use
//                   Firebase's database secret / no-auth mode.
//                   If security matters, upgrade to a SIM800L with SSL
//                   firmware (AT+CCHSTART) or use an HTTP-to-HTTPS proxy.
//
//  3. AT+CIPSEND — send raw bytes over the open TCP connection.
//
//  4. AT+CIPCLOSE — close the connection.
//
//  The UART (sim800) is shared with SMS.  The mutex used for simStatus does
//  NOT protect UART access because SMS and GPRS are never called concurrently:
//  SMS is triggered only from the main loop during an emergency, and GPRS
//  uploads happen on the simStatusTask (Core 0) via uploadToFirebase().
//  If you ever call both from different tasks simultaneously you will need
//  a second UART mutex.

static bool gprsActive = false;  // true after a successful AT+SAPBR open

// ── Bring up GPRS bearer ─────────────────────────────────────────────────────
bool initGPRS() {
  String apn = settingsManager.get().simAPN;
  if (apn.length() == 0) {
    Serial.println("[GPRS] No APN configured — skipping GPRS init.");
    return false;
  }
  if (!simStatus.registered) {
    Serial.println("[GPRS] Not registered — cannot start GPRS.");
    return false;
  }

  Serial.println("[GPRS] Bringing up bearer with APN: " + apn);

  // Close any stale bearer first (ignore errors)
  atCmd("AT+SAPBR=0,1", "OK", 5000);
  delay(500);

  // Set bearer parameters
  atCmd("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK", 2000);
  String apnCmd = "AT+SAPBR=3,1,\"APN\",\"" + apn + "\"";
  atCmd(apnCmd.c_str(), "OK", 2000);

  // Open bearer — can take up to 30s on a cold start
  if (!atCmd("AT+SAPBR=1,1", "OK", 30000)) {
    Serial.println("[GPRS] Bearer open FAILED.");
    gprsActive = false;
    return false;
  }

  // Query assigned IP
  String ipResp = atQuery("AT+SAPBR=2,1", 3000);
  Serial.println("[GPRS] IP: " + ipResp);

  // Response: +SAPBR: 1,1,"<ip>" — status 1 = connected
  if (ipResp.indexOf(",1,") < 0) {
    Serial.println("[GPRS] No IP assigned.");
    gprsActive = false;
    return false;
  }

  gprsActive = true;
  Serial.println("[GPRS] Bearer up. GPRS ready.");
  return true;
}

bool gprsConnected() { return gprsActive; }

// ── HTTP POST over GPRS TCP ───────────────────────────────────────────────────
// Uses AT+CIPSTART (single-connection TCP mode).
// Returns HTTP status code, or -1 on failure.
int httpPostGPRS(const String& host, const String& path,
                 const String& body, unsigned long timeoutMs) {

  if (!gprsActive) {
    Serial.println("[GPRS] httpPost called but bearer not active.");
    return -1;
  }

  Serial.println("[GPRS] POST " + host + path);

  // ── 1. Ensure TCP stack is in single-connection mode ──
  atCmd("AT+CIPMUX=0", "OK", 2000);

  // ── 2. Set TCP bearer profile ──
  String csttCmd = "AT+CSTT=\"" + settingsManager.get().simAPN + "\"";
  atCmd(csttCmd.c_str(), "OK", 5000);
  atCmd("AT+CIICR", "OK", 10000);   // Activate wireless connection
  // (These are no-ops if already active — SIM800L ignores them gracefully.)

  // ── 3. Open TCP connection ──
  String cipCmd = "AT+CIPSTART=\"TCP\",\"" + host + "\",\"80\"";
  if (!atCmd(cipCmd.c_str(), "CONNECT", 15000)) {
    Serial.println("[GPRS] TCP connect failed.");
    gprsActive = false;   // bearer may have dropped; force re-init next cycle
    return -1;
  }

  // ── 4. Build the HTTP/1.1 request ─────────────────────────────────────────
  // Firebase REST API over plain HTTP (port 80).
  // Content-Type must be application/json; Content-Length must be exact.
  String req = "";
  req += "POST " + path + " HTTP/1.1\r\n";
  req += "Host: " + host + "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "Connection: close\r\n";
  req += "\r\n";
  req += body;

  // ── 5. Send data ──────────────────────────────────────────────────────────
  String sendCmd = "AT+CIPSEND=" + String(req.length());
  sim800.println(sendCmd);
  Serial.println("[GPRS] >> " + sendCmd);

  // Wait for the '>' prompt
  if (!waitFor('>', 5000)) {
    Serial.println("[GPRS] No > prompt from CIPSEND.");
    atCmd("AT+CIPCLOSE", "OK", 5000);
    return -1;
  }

  sim800.print(req);
  sim800.write(SMS_CTRL_Z);   // CTRL-Z terminates the send
  Serial.println("[GPRS] Request sent (" + String(req.length()) + " bytes).");

  // ── 6. Read response and extract status code ──────────────────────────────
  // We only need the first line: "HTTP/1.1 200 OK"
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (sim800.available()) resp += (char)sim800.read();
    if (resp.indexOf("HTTP/1.") >= 0 && resp.indexOf("\r\n") >= 0) break;
    delay(10);
  }
  Serial.println("[GPRS] Response: " + resp.substring(0, min((int)resp.length(), 200)));

  // Close connection
  atCmd("AT+CIPCLOSE", "OK", 5000);

  // Parse status code from "HTTP/1.1 200 OK"
  int httpIdx = resp.indexOf("HTTP/1.");
  if (httpIdx < 0) return -1;
  int spaceIdx = resp.indexOf(' ', httpIdx);
  if (spaceIdx < 0) return -1;
  int code = resp.substring(spaceIdx + 1, spaceIdx + 4).toInt();
  Serial.println("[GPRS] HTTP status: " + String(code));
  return code;
}