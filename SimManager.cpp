#include "SimManager.h"
#include "Sensors.h"     
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

HardwareSerial sim800(2);
static SimStatus simStatus;
SemaphoreHandle_t simStatusMutex = nullptr;

static constexpr uint8_t SMS_CTRL_Z = 26;    

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

bool syncTimeFromSIM() {
  Serial.println("[SIM] Syncing RTC from network time...");
  atCmd("AT+CLTS=1", "OK", 2000);
  atCmd("AT&W",      "OK", 2000);
  delay(2000); 

  String resp = atQuery("AT+CCLK?", 2000);
  Serial.println("[SIM] CCLK: " + resp);

  int q1 = resp.indexOf('"');
  int q2 = resp.indexOf('"', q1 + 1);
  if (q1 < 0 || q2 <= q1) {
    Serial.println("[SIM] RTC sync failed.");
    return false;
  }

  String ts = resp.substring(q1 + 1, q2); 
  if (ts.length() < 17) return false;

  uint8_t yr = ts.substring(0,  2).toInt();
  uint8_t mo = ts.substring(3,  5).toInt();
  uint8_t dd = ts.substring(6,  8).toInt();
  uint8_t hh = ts.substring(9,  11).toInt();
  uint8_t mn = ts.substring(12, 14).toInt();
  uint8_t ss = ts.substring(15, 17).toInt();

  if (yr == 0 && mo == 0) return false;

  static const unsigned long LKT_OFFSET_SEC = 19800UL; 
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
  unsigned long unixUTC = unixLKT - LKT_OFFSET_SEC; 
  setSoftClock(unixUTC);

  return true;
}

void initSimManager() {
  simStatusMutex = xSemaphoreCreateMutex();

  sim800.begin(9600, SERIAL_8N1, SIM800_RX_PIN, SIM800_TX_PIN);
  Serial.println("[SIM] Waiting for module power-on (5s)...");
  delay(5000);

  bool alive = false;
  for (int i = 0; i < 5; i++) {
    if (atCmd("AT", "OK", 2000)) { alive = true; break; }
    delay(1000);
  }
  if (!alive) {
    Serial.println("[SIM] ERROR: No response.");
    return;
  }

  atCmd("ATE0", "OK");
  delay(200);

  if (!atCmd("AT+CPIN?", "READY", 3000)) {
    Serial.println("[SIM] ERROR: SIM not ready.");
    return;
  }

  Serial.println("[SIM] Waiting for network registration...");
  for (int i = 0; i < 20; i++) {
    String reg = atQuery("AT+CREG?", 1500);
    if (reg.indexOf(",1") >= 0 || reg.indexOf(",5") >= 0) {
      xSemaphoreTake(simStatusMutex, portMAX_DELAY);
      simStatus.registered = true;
      xSemaphoreGive(simStatusMutex);
      Serial.println("[SIM] Registered to network."); break;
    }
    delay(2000);
  }

  if (simStatus.registered) syncTimeFromSIM();
  Serial.println("[SIM] Init complete.");
}

SimStatus getSimStatus() {
  xSemaphoreTake(simStatusMutex, portMAX_DELAY);
  SimStatus snap = simStatus;
  xSemaphoreGive(simStatusMutex);
  return snap;
}

static void transmitSMS(const String& phone, const String& msg) {
  if (!atCmd("AT", "OK", 3000)) return;
  String reg = atQuery("AT+CREG?", 1500);
  if (reg.indexOf(",1") < 0 && reg.indexOf(",5") < 0) return;
  if (!atCmd("AT+CMGF=1", "OK")) return;

  String cmgsCmd = "AT+CMGS=\"" + phone + "\"";
  sim800.println(cmgsCmd);
  if (!waitFor('>', 5000)) return;

  sim800.print(msg);
  delay(200);
  sim800.write(SMS_CTRL_Z);

  String sendResp = "";
  unsigned long start = millis();
  while (millis() - start < 15000) {
    while (sim800.available()) sendResp += (char)sim800.read();
    if (sendResp.indexOf("+CMGS") >= 0) { Serial.println("[SIM] SMS sent!"); return; }
    if (sendResp.indexOf("ERROR") >= 0) return;
  }
}

void sendLocationSMS(const SensorData& data, SosEventType eventType) {

  // ── Resolve best available location ──────────────────────────────
  float  lat = (float)data.lat;
  float  lon = (float)data.lon;
  bool   gpsValid = (lat != 0.0f || lon != 0.0f);
  String locSrc;

  if (!gpsValid && webLocationInjected &&
      (webInjectedLat != 0.0 || webInjectedLon != 0.0)) {
    lat      = (float)webInjectedLat;
    lon      = (float)webInjectedLon;
    gpsValid = true;
    locSrc   = "Web/Phone GPS";
  } else if (gpsValid) {
    locSrc = "On-board GPS";
  } else {
    locSrc = "Unavailable";
  }

  // ── Resolve local time (LKT = UTC+5:30) ──────────────────────────
  static constexpr unsigned long LKT_OFFSET_SEC = 19800UL;
  unsigned long elapsed = (millis() - softClockSetAt) / 1000UL;
  unsigned long t = softClockBase + elapsed + LKT_OFFSET_SEC;

  uint8_t ss   = t % 60;
  uint8_t mn   = (t / 60)   % 60;
  uint8_t hh   = (t / 3600) % 24;
  uint32_t days = t / 86400UL;
  uint16_t year = 1970;
  while (true) {
    bool leap = (year%4==0 && (year%100!=0 || year%400==0));
    uint32_t diy = leap ? 366 : 365;
    if (days < diy) break;
    days -= diy; year++;
  }
  bool leap = (year%4==0 && (year%100!=0 || year%400==0));
  static const uint8_t dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  uint8_t month = 1;
  for (; month <= 12; month++) {
    uint8_t md = dim[month-1] + (month==2 && leap ? 1 : 0);
    if (days < md) break;
    days -= md;
  }
  uint8_t day = (uint8_t)(days + 1);

  char timeBuf[10], dateBuf[14];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", hh, mn, ss);
  snprintf(dateBuf, sizeof(dateBuf), "%02d/%02d/%04d",  day, month, year);

  // ── Event type label & severity ──────────────────────────────────
  String eventLabel, severity;
  switch (eventType) {
    case SOS_ROLLOVER:
      eventLabel = "VEHICLE ROLLOVER";
      severity   = "CRITICAL — Vehicle may be overturned";
      break;
    case SOS_MANUAL:
      eventLabel = "MANUAL SOS";
      severity   = "HIGH — Driver triggered emergency alert";
      break;
    case SOS_IMPACT:
    default:
      eventLabel = "COLLISION / IMPACT";
      severity   = "HIGH — Sudden high-G impact detected";
      break;
  }

  // ── Coord strings ─────────────────────────────────────────────────
  String coordStr   = gpsValid ? (String(lat, 5) + "," + String(lon, 5)) : "";
  String mapsLink   = gpsValid ? ("maps.google.com/?q=" + coordStr)       : "";
  String hospiLink  = gpsValid ? ("maps.google.com/?q=hospital+"  + coordStr) : "";
  String policeLink = gpsValid ? ("maps.google.com/?q=police+"    + coordStr) : "";

  // Live dashboard tracking link (always included regardless of GPS)
  String liveLink = "axelaix.netlify.app/#/emergency?uid=" + String(USER_ID);

  // ── Build message ─────────────────────────────────────────────────
  String msg = "";

  msg += "*** AXEL BLACKBOX ALERT ***\n";
  msg += "============================\n";
  msg += "EVENT  : " + eventLabel + "\n";
  msg += "SEVERITY: " + severity + "\n";
  msg += "============================\n\n";

  msg += "-- INCIDENT DETAILS --\n";
  msg += "Date   : " + String(dateBuf) + "\n";
  msg += "Time   : " + String(timeBuf) + " (LKT)\n";
  msg += "Speed  : " + String((int)data.speed) + " km/h\n";
  msg += "Loc Src: " + locSrc + "\n\n";

  // Live tracking — always first, always prominent
  msg += "-- LIVE TRACKING --\n";
  msg += liveLink + "\n";
  msg += "(Real-time location & sensor data)\n\n";

  if (gpsValid) {
    msg += "-- SNAPSHOT LOCATION --\n";
    msg += mapsLink + "\n";
    msg += "Coords: " + coordStr + "\n\n";

    msg += "-- NEARBY HELP --\n";
    msg += "Hospitals : " + hospiLink + "\n";
    msg += "Police    : " + policeLink + "\n\n";
  } else {
    msg += "-- LOCATION --\n";
    msg += "GPS fix unavailable.\n";
    msg += "Use live tracker above.\n\n";
  }

  msg += "============================\n";
  msg += "Powered by Axel BlackBox";

  transmitSMS(PHONE_NUMBER, msg);
}

static bool gprsActive = false;  

bool initGPRS() {
  String apn = SIM_APN;
  if (!simStatus.registered) return false;

  atCmd("AT+SAPBR=0,1", "OK", 5000);
  delay(500);

  atCmd("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK", 2000);
  String apnCmd = "AT+SAPBR=3,1,\"APN\",\"" + apn + "\"";
  atCmd(apnCmd.c_str(), "OK", 2000);

  if (!atCmd("AT+SAPBR=1,1", "OK", 30000)) {
    gprsActive = false;
    return false;
  }

  String ipResp = atQuery("AT+SAPBR=2,1", 3000);
  if (ipResp.indexOf(",1,") < 0) {
    gprsActive = false;
    return false;
  }

  gprsActive = true;
  return true;
}

bool gprsConnected() { return gprsActive; }

int httpPostGPRS(const String& host, const String& path, const String& body, unsigned long timeoutMs) {
  if (!gprsActive) return -1;

  atCmd("AT+CIPMUX=0", "OK", 2000);
  String csttCmd = "AT+CSTT=\"" + String(SIM_APN) + "\"";
  atCmd(csttCmd.c_str(), "OK", 5000);
  atCmd("AT+CIICR", "OK", 10000);   

  String cipCmd = "AT+CIPSTART=\"TCP\",\"" + host + "\",\"80\"";
  if (!atCmd(cipCmd.c_str(), "CONNECT", 15000)) {
    gprsActive = false;  
    return -1;
  }

  String req = "";
  req += "POST " + path + " HTTP/1.1\r\n";
  req += "Host: " + host + "\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: " + String(body.length()) + "\r\n";
  req += "Connection: close\r\n";
  req += "\r\n";
  req += body;

  String sendCmd = "AT+CIPSEND=" + String(req.length());
  sim800.println(sendCmd);

  if (!waitFor('>', 5000)) {
    atCmd("AT+CIPCLOSE", "OK", 5000);
    return -1;
  }

  sim800.print(req);
  sim800.write(SMS_CTRL_Z);   

  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (sim800.available()) resp += (char)sim800.read();
    if (resp.indexOf("HTTP/1.") >= 0 && resp.indexOf("\r\n") >= 0) break;
    delay(10);
  }

  atCmd("AT+CIPCLOSE", "OK", 5000);

  int httpIdx = resp.indexOf("HTTP/1.");
  if (httpIdx < 0) return -1;
  int spaceIdx = resp.indexOf(' ', httpIdx);
  if (spaceIdx < 0) return -1;
  int code = resp.substring(spaceIdx + 1, spaceIdx + 4).toInt();
  return code;
}