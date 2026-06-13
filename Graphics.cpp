#include "Graphics.h"
#include "SimManager.h"
#include <WiFi.h> // Required to check WiFi status in the simplified UI

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, SCL_PIN, SDA_PIN);

// --- ASSETS ---
const unsigned char heart_bits[] = { 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00 };

// Face Variables
const int centerX = 64; const int centerY = 32;
int baseWidth = 28; int baseHeight = 38; int eyeGap = 12;     
float curX=0, curY=0, curLSc=1.0, curRSc=1.0, curLid=0; 
float tarX=0, tarY=0, tarLSc=1.0, tarRSc=1.0, tarLid=0;
float moveEase=7.0, squashEase=5.0, lidEase=5.0;    
unsigned long lastStateTime = 0;
int stateStep = 0;

void initDisplay() { u8g2.begin(); }

// --- HELPER ---
void showCalibratingText() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(25, 30, "CALIBRATING...");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(30, 45, "KEEP STILL");
  u8g2.sendBuffer();
}

// --- STARTUP ANIMATION ---
void playStartupAnimation() {
  u8g2.clearBuffer(); u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(20, 35, "SYSTEM CHECK..."); u8g2.sendBuffer(); delay(800);
  
  for (int r = 0; r <= 25; r += 2) {
    u8g2.clearBuffer(); u8g2.drawCircle(64, 32, r); u8g2.sendBuffer(); delay(10);
  }
  for (int i = 0; i <= 20; i++) {
    u8g2.clearBuffer(); u8g2.drawCircle(64, 32, 25); 
    if (i <= 10) {
        int x = map(i, 0, 10, 48, 60); int y = map(i, 0, 10, 32, 44);
        u8g2.drawLine(48, 32, x, y);
    } else {
        u8g2.drawLine(48, 32, 60, 44); 
    }
    if (i > 10) {
        int x = map(i, 10, 20, 60, 82); int y = map(i, 10, 20, 44, 20);
        u8g2.drawLine(60, 44, x, y);
    }
    u8g2.sendBuffer(); delay(20);
  }
  delay(1000);
}

// --- PAGE 0: CLOCK ---
void drawClockPage(const SensorData& data) {
  u8g2.clearBuffer();
  
  // Big Time
  u8g2.setFont(u8g2_font_logisoso24_tf); 
  int w = u8g2.getStrWidth(data.timeStr.c_str());
  u8g2.setCursor((128 - w) / 2, 42); 
  u8g2.print(data.timeStr);

  // Date below
  u8g2.setFont(u8g2_font_6x10_tf);
  w = u8g2.getStrWidth(data.dateStr.c_str());
  u8g2.setCursor((128 - w) / 2, 60);
  u8g2.print(data.dateStr);

  // Top Bar
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, "CURRENT TIME");
  u8g2.drawLine(0, 10, 128, 10);

  u8g2.sendBuffer();
}

// --- PAGE 1: ACCELEROMETER ---
void drawAccelPage(const SensorData& data) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, "ACCELEROMETER"); u8g2.drawLine(0, 10, 128, 10);

  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.setCursor(0, 30); u8g2.print("X: "); u8g2.print(data.ax, 2);
  u8g2.setCursor(0, 45); u8g2.print("Y: "); u8g2.print(data.ay, 2);
  u8g2.setCursor(0, 60); u8g2.print("Z: "); u8g2.print(data.az, 2);
  u8g2.sendBuffer();
}

// --- PAGE 2: GYROSCOPE ---
void drawGyroPage(const SensorData& data) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, "GYROSCOPE"); u8g2.drawLine(0, 10, 128, 10);

  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.setCursor(0, 30); u8g2.print("X: "); u8g2.print(data.gx, 2);
  u8g2.setCursor(0, 45); u8g2.print("Y: "); u8g2.print(data.gy, 2);
  u8g2.setCursor(0, 60); u8g2.print("Z: "); u8g2.print(data.gz, 2);
  u8g2.sendBuffer();
}

// --- PAGE 3: FLIGHT NAV / SPEEDOMETER ---
void drawGPSPage(const SensorData& data) {
  u8g2.clearBuffer();

  // Resolve which location source to display
  bool  useWeb  = (!data.gpsValid && data.webLocationValid);
  double dispLat = data.gpsValid ? data.lat   : (useWeb ? data.webLat   : 0.0);
  double dispLon = data.gpsValid ? data.lon   : (useWeb ? data.webLon   : 0.0);
  float  dispSpd = data.gpsValid ? data.speed : (useWeb ? data.webSpeed : 0.0f);

  // Top Header Bar — show source label
  u8g2.setDrawColor(1); u8g2.drawBox(0, 0, 128, 11);
  u8g2.setDrawColor(0); u8g2.setFont(u8g2_font_5x7_tf);
  if (data.gpsValid) {
    u8g2.setCursor(2, 8); u8g2.print("SATS:"); u8g2.print(data.sats);
    u8g2.drawStr(84, 8, "GPS NAV");
  } else if (useWeb) {
    u8g2.drawStr(2, 8, "NO GPS");
    u8g2.drawStr(72, 8, "WEB LOC");
  } else {
    u8g2.drawStr(2, 8, "NO GPS");
    u8g2.drawStr(68, 8, "NO FIX");
  }
  u8g2.setDrawColor(1);

  // Center Divider Line
  u8g2.drawLine(64, 12, 64, 64);

  // LEFT SIDE: Coordinates
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 22, "LATITUDE");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(2, 34);
  if (!data.gpsValid && !useWeb) u8g2.print("---.-----");
  else u8g2.print(dispLat, 5);

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 48, "LONGITUDE");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(2, 60);
  if (!data.gpsValid && !useWeb) u8g2.print("---.-----");
  else u8g2.print(dispLon, 5);

  // RIGHT SIDE: Speedometer
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(74, 22, "SPEED km/h");

  u8g2.setFont(u8g2_font_logisoso24_tf);
  int currentSpeed = (int)dispSpd;
  if (currentSpeed < 0) currentSpeed = 0;

  String spdStr = String(currentSpeed);
  int w = u8g2.getStrWidth(spdStr.c_str());
  u8g2.setCursor(64 + (32 - (w/2)), 55);
  u8g2.print(spdStr);

  u8g2.sendBuffer();
}

// --- PAGE 4: SYSTEM HEALTH (storage + temperature) ---
void drawSystemPage(const SensorData& data) {
  u8g2.clearBuffer();

  // ── Header ──────────────────────────────────────────────────────
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, "SYSTEM HEALTH");
  u8g2.drawLine(0, 10, 128, 10);

  // ── Left column: Storage ────────────────────────────────────────
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 21, "STORAGE");

  u8g2.setFont(u8g2_font_6x10_tf);
  if (!data.sdStatus) {
    u8g2.drawStr(0, 33, "NO STORAGE");
  } else {
    u8g2.setCursor(0, 33);
    u8g2.print(data.sdUsedMB, 1); u8g2.print("/");
    u8g2.print(data.sdTotalMB, 1); u8g2.print("MB");

    // Storage bar (left half of screen, y=38)
    if (data.sdTotalMB > 0) {
      int w = (int)((data.sdUsedMB / data.sdTotalMB) * 58.0f);
      if (w < 2 && data.sdUsedMB > 0) w = 2;
      if (w > 58) w = 58;
      u8g2.drawFrame(0, 38, 58, 8);
      u8g2.drawBox(0, 38, w, 8);
    }
  }

  // ── Vertical divider ────────────────────────────────────────────
  u8g2.drawLine(64, 11, 64, 63);

  // ── Right column: Temperature ───────────────────────────────────
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(68, 21, "TEMP (die)");

  // Large temperature value
  u8g2.setFont(u8g2_font_9x15_tf);
  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%.1f", data.temp);
  u8g2.setCursor(68, 35);
  u8g2.print(tbuf);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.print(" C");

  // Temperature bar (right half, y=38) — range 20°C to 90°C
  {
    const float tLow = 20.0f, tHigh = 90.0f;
    float norm = (data.temp - tLow) / (tHigh - tLow);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    int w = (int)(norm * 58.0f);
    u8g2.drawFrame(66, 38, 60, 8);
    u8g2.drawBox(66, 38, w, 8);

    // Threshold tick mark inside the bar
    int tickX = 66 + (int)(((TEMP_ALERT_THRESHOLD_C - tLow) / (tHigh - tLow)) * 60.0f);
    if (tickX > 66 && tickX < 126) {
      u8g2.drawLine(tickX, 36, tickX, 47); // tick straddles the bar
    }
  }

  // ── Warning label if over threshold ─────────────────────────────
  u8g2.setFont(u8g2_font_5x7_tf);
  if (data.temp >= TEMP_ALERT_THRESHOLD_C) {
    // Blinking "HOT!" label
    if ((millis() / 400) % 2 == 0) {
      u8g2.drawStr(74, 55, ">>> HOT! <<<");
    }
  } else {
    u8g2.drawStr(74, 55, "NORMAL");
  }

  // ── Bottom: free space percentage ───────────────────────────────
  if (data.sdStatus && data.sdTotalMB > 0) {
    u8g2.setFont(u8g2_font_5x7_tf);
    int pct = (int)((data.sdUsedMB / data.sdTotalMB) * 100.0f);
    char pbuf[10];
    snprintf(pbuf, sizeof(pbuf), "%d%% used", pct);
    u8g2.drawStr(0, 55, pbuf);
  }

  u8g2.sendBuffer();
}

// --- FACE LOGIC ---
void setTargets(float x, float y, float lSc, float rSc, float lid) {
  tarX = x; tarY = y; tarLSc = lSc; tarRSc = rSc; tarLid = lid;
}
bool wait(unsigned long now, int duration) { return (now - lastStateTime > duration); }
void nextStep() { stateStep++; lastStateTime = millis(); }

void drawEyes(float x, float y, float lS, float rS, float lid) {
  int lH = baseHeight * lS; if(lH<2) lH=2;
  int lW = baseWidth * (1.0 + ((1.0 - lS) * 0.4));
  int lx = centerX - (eyeGap/2) - lW + (int)x;
  int ly = centerY - (lH/2) + (int)y;
  int rH = baseHeight * rS; if(rH<2) rH=2;
  int rW = baseWidth * (1.0 + ((1.0 - rS) * 0.4));
  int rx = centerX + (eyeGap/2) + (int)x;
  int ry = centerY - (rH/2) + (int)y;

  if (lH < 6) u8g2.drawBox(lx, ly, lW, lH);
  else { int rad=lH/3; if(rad>8) rad=8; u8g2.drawRBox(lx, ly, lW, lH, rad); }
  if (rH < 6) u8g2.drawBox(rx, ry, rW, rH);
  else { int rad=rH/3; if(rad>8) rad=8; u8g2.drawRBox(rx, ry, rW, rH, rad); }
  
  if (lid > 1) {
    u8g2.setDrawColor(0); 
    int h = (int)lid; if (h >= lH) h = lH - 1; 
    u8g2.drawBox(lx, ly + lH - h, lW, h);
    h = (int)lid; if (h >= rH) h = rH - 1;
    u8g2.drawBox(rx, ry + rH - h, rW, h);
  }
}

void drawFace(const SensorData& data, bool isHappy) {
  unsigned long now = millis();
  if (isHappy) {
    setTargets(0, 0, 1.1, 1.1, 25);
    moveEase = 3.0; lidEase = 3.0;
  } else {
    moveEase = 7.0; lidEase = 5.0;
    switch(stateStep) {
      case 0: setTargets(0, 0, 1.0, 1.0, 0); if(wait(now, 1200)) nextStep(); break;
      case 1: setTargets(-18, 0, 1.0, 1.0, 0); if(wait(now, 1000)) nextStep(); break;
      case 2: setTargets(-18, 0, 0.1, 0.1, 0); if(wait(now, 200)) nextStep(); break;
      case 3: setTargets(0, 0, 1.0, 1.0, 0); if(wait(now, 800)) nextStep(); break;
      case 4: setTargets(18, 0, 1.0, 1.0, 0); if(wait(now, 1000)) nextStep(); break;
      case 5: setTargets(0, 0, 1.0, 1.0, 0); if(wait(now, 800)) nextStep(); break;
      case 6: stateStep = 0; break; 
    }
  }
  curX += (tarX - curX) / moveEase; curY += (tarY - curY) / moveEase;
  curLSc += (tarLSc - curLSc) / squashEase; curRSc += (tarRSc - curRSc) / squashEase;
  curLid += (tarLid - curLid) / lidEase;
  u8g2.clearBuffer(); u8g2.setDrawColor(1);
  drawEyes(curX, curY, curLSc, curRSc, curLid);
  if (isHappy) {
    int fy = sin(millis() / 200.0) * 3;
    u8g2.drawXBMP(4, 28 + fy, 8, 8, heart_bits); u8g2.drawXBMP(116, 28 + fy, 8, 8, heart_bits);
    u8g2.drawCircle(centerX, centerY+15, 10, U8G2_DRAW_LOWER_LEFT | U8G2_DRAW_LOWER_RIGHT);
  }
  u8g2.sendBuffer();
}

// --- EMERGENCY SCREEN 1: IMPACT ---
void drawCrashPage(float gForce) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1); u8g2.drawBox(0, 0, 128, 14);
  u8g2.setDrawColor(0); u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 11, "!!! IMPACT !!!");
  u8g2.setDrawColor(1);
  
  u8g2.setFont(u8g2_font_logisoso24_tf);
  String gStr = String(gForce, 1) + "G";
  int w = u8g2.getStrWidth(gStr.c_str());
  u8g2.setCursor((128-w)/2, 45);
  u8g2.print(gStr);
  
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(25, 60, "CRASH DETECTED");
  u8g2.sendBuffer();
}

// --- EMERGENCY SCREEN 2: COUNTDOWN ---
void drawCountdownPage(int secondsLeft) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(15, 12, "SENDING SOS IN:");
  
  u8g2.setFont(u8g2_font_logisoso24_tf);
  String timeStr = String(secondsLeft);
  int w = u8g2.getStrWidth(timeStr.c_str());
  u8g2.setCursor((128-w)/2, 45);
  u8g2.print(timeStr);
  
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(15, 60, "> TAP TO CANCEL <");
  u8g2.sendBuffer();
}

// --- EMERGENCY SCREEN 3: FALSE ALARM / SAFE ---
void drawDriveSafePage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.drawStr(10, 30, "SOS CANCELED");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(30, 50, "DRIVE SAFE");
  u8g2.sendBuffer();
}

// --- EMERGENCY SCREEN: SENDING IN PROGRESS ---
void drawSendingPage() {
  u8g2.clearBuffer();

  u8g2.drawRFrame(0, 0, 128, 64, 4); // Rounded outer frame
  u8g2.drawLine(4, 12, 124, 12);     // Header underline
  
  u8g2.setFont(u8g2_font_5x7_tf);
  int w1 = u8g2.getStrWidth("EMERGENCY OVERRIDE");
  u8g2.setCursor((128 - w1) / 2, 9);
  u8g2.print("EMERGENCY OVERRIDE");

  u8g2.setFont(u8g2_font_6x10_tf);
  int w2 = u8g2.getStrWidth("SENDING SOS...");
  u8g2.setCursor((128 - w2) / 2, 32);
  u8g2.print("SENDING SOS...");

  // Blinking "Please Wait" footer
  if ((millis() / 400) % 2 == 0) {
    u8g2.setDrawColor(1);
    u8g2.drawBox(10, 46, 108, 14);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tf);
    int w3 = u8g2.getStrWidth("PLEASE WAIT");
    u8g2.setCursor((128 - w3) / 2, 56);
    u8g2.print("PLEASE WAIT");
    u8g2.setDrawColor(1); 
  } else {
    u8g2.setFont(u8g2_font_5x7_tf);
    int w3 = u8g2.getStrWidth("PLEASE WAIT");
    u8g2.setCursor((128 - w3) / 2, 56);
    u8g2.print("PLEASE WAIT");
  }

  u8g2.sendBuffer();
}

// --- EMERGENCY SCREEN 4: SMS SENT (Upgraded) ---
void drawCallingPage() {
  u8g2.clearBuffer();

  // 1. Tech Border & Header
  u8g2.drawRFrame(0, 0, 128, 64, 4); // Rounded outer frame
  u8g2.drawLine(4, 12, 124, 12);     // Header underline
  
  u8g2.setFont(u8g2_font_5x7_tf);
  int w1 = u8g2.getStrWidth("SOS TRANSMITTED");
  u8g2.setCursor((128 - w1) / 2, 9);
  u8g2.print("SOS TRANSMITTED");

  // 2. Envelope Icon (Left Side)
  int envX = 14;
  int envY = 24;
  u8g2.drawFrame(envX, envY, 24, 16);
  u8g2.drawLine(envX, envY, envX + 12, envY + 8);         // Left flap
  u8g2.drawLine(envX + 24, envY, envX + 12, envY + 8);    // Right flap

  // 3. Animated "Radio Waves" above the envelope
  // Uses millis() to cycle through 4 frames of animation
  int tick = (millis() / 300) % 4; 
  if (tick > 0) { u8g2.drawLine(envX + 10, envY - 3, envX + 14, envY - 3); }
  if (tick > 1) { u8g2.drawLine(envX + 8,  envY - 6, envX + 16, envY - 6); }
  if (tick > 2) { u8g2.drawLine(envX + 6,  envY - 9, envX + 18, envY - 9); }

  // 4. Main Text (Right Side)
  u8g2.setFont(u8g2_font_9x15_tf);
  u8g2.drawStr(46, 36, "SMS SENT!");

  // 5. Blinking Footer (Flashing inverted box)
  if ((millis() / 500) % 2 == 0) {
    // Draw inverted (White box, black text)
    u8g2.setDrawColor(1);
    u8g2.drawBox(10, 46, 108, 14);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tf);
    int w2 = u8g2.getStrWidth("TAP TO RESET SYSTEM");
    u8g2.setCursor((128 - w2) / 2, 56);
    u8g2.print("TAP TO RESET SYSTEM");
    u8g2.setDrawColor(1); // Reset color back to normal for next frame
  } else {
    // Draw normal (Black box, white text)
    u8g2.setFont(u8g2_font_5x7_tf);
    int w2 = u8g2.getStrWidth("TAP TO RESET SYSTEM");
    u8g2.setCursor((128 - w2) / 2, 56);
    u8g2.print("TAP TO RESET SYSTEM");
  }

  u8g2.sendBuffer();
}

// =====================================================
// CONNECTING ANIMATION — shown while SIM init blocks
// Called from a FreeRTOS task in setup()
// =====================================================
void drawConnectingAnimation() {
  static uint8_t frame = 0;
  frame = (frame + 1) % 8;

  u8g2.clearBuffer();

  // Title
  u8g2.setFont(u8g2_font_6x10_tf);
  int tw = u8g2.getStrWidth("CONNECTING SIM");
  u8g2.setCursor((128 - tw) / 2, 18);
  u8g2.print("CONNECTING SIM");

  // Animated signal bars — each bar lights up in sequence
  // 5 bars, cycling frame by frame
  for (int b = 0; b < 5; b++) {
    int h  = 6 + (b * 5);        // 6,11,16,21,26 px tall
    int bx = 39 + b * 12;
    int by = 52 - h;
    // Bar is "on" if it's within the current sweep window
    bool lit = (b <= (int)(frame % 6));
    if (lit) u8g2.drawBox(bx, by, 8, h);
    else     u8g2.drawFrame(bx, by, 8, h);
  }

  // Animated dots under the bars: "   " → ".  " → ".. " → "..."
  u8g2.setFont(u8g2_font_5x7_tf);
  const char* dots[] = {"   ", ".  ", ".. ", "..."};
  int dotIdx = (frame / 2) % 4;
  int dw = u8g2.getStrWidth("...");
  u8g2.setCursor((128 - dw) / 2, 63);
  u8g2.print(dots[dotIdx]);

  u8g2.sendBuffer();
}

// =====================================================
// PAGE 5: SIM STATUS — Minimal Network Status
// =====================================================
void drawSimPage(SimStatus sim) {
  u8g2.clearBuffer();

  // ── Top bar (inverted) ──────────────────────────────
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 13);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(3, 10, "NETWORK STATUS");
  u8g2.setDrawColor(1);

  u8g2.setFont(u8g2_font_6x10_tf);

  // ── WiFi Status ──────────────────────────────────────
  u8g2.setCursor(10, 35);
  if (WiFi.status() == WL_CONNECTED) {
    u8g2.print("WIFI: CONNECTED");
  } else {
    u8g2.print("WIFI: OFFLINE");
  }

  // ── SIM Status ───────────────────────────────────────
  u8g2.setCursor(10, 55);
  if (sim.registered) {
    u8g2.print("SIM:  ONLINE");
  } else {
    u8g2.print("SIM:  SEARCHING");
  }

  u8g2.sendBuffer();
}

// =====================================================
// ROLLOVER ALERT SCREEN
// Shows roll and pitch angles with a visual attitude
// indicator (a tilted rectangle representing the car).
// =====================================================
void drawRolloverPage(float roll, float pitch) {
  u8g2.clearBuffer();

  // ── Inverted alarm header ──
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 14);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(8, 11, "!!! ROLLOVER !!!");
  u8g2.setDrawColor(1);

  // ── Attitude indicator — tilted rectangle centred at (96, 39) ──
  // The rectangle represents the vehicle cross-section.
  // We rotate its four corners by the roll angle so it visually tips over.
  const float cx = 96.0f, cy = 39.0f;
  const float hw = 14.0f, hh = 8.0f;   // half-width, half-height
  float rad = roll * (M_PI / 180.0f);
  float cosR = cosf(rad), sinR = sinf(rad);

  // Four corners before rotation: (±hw, ±hh)
  float cornersX[4] = { -hw,  hw,  hw, -hw };
  float cornersY[4] = { -hh, -hh,  hh,  hh };
  int px[4], py[4];
  for (int i = 0; i < 4; i++) {
    px[i] = (int)(cx + cornersX[i] * cosR - cornersY[i] * sinR);
    py[i] = (int)(cy + cornersX[i] * sinR + cornersY[i] * cosR);
  }
  // Draw the rotated rectangle as four lines
  for (int i = 0; i < 4; i++) {
    u8g2.drawLine(px[i], py[i], px[(i+1)%4], py[(i+1)%4]);
  }
  // Horizon reference line (always horizontal)
  u8g2.drawLine(74, 39, 118, 39);

  // ── Numeric angle readout (left side) ──
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(2, 26, "ROLL:");
  u8g2.setCursor(2, 38);
  u8g2.print(roll, 1); u8g2.print((char)0xB0); // degree symbol

  u8g2.drawStr(2, 48, "PITCH:");
  u8g2.setCursor(2, 58);
  u8g2.print(pitch, 1); u8g2.print((char)0xB0);

  // ── Flashing "TAP TO DISMISS" footer ──
  if ((millis() / 500) % 2 == 0) {
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawBox(0, 57, 128, 7);
    u8g2.setDrawColor(0);
    u8g2.drawStr(14, 63, "TAP TO DISMISS");
    u8g2.setDrawColor(1);
  }

  u8g2.sendBuffer();
}

// =====================================================
// TEMPERATURE ALERT SCREEN
// Shows current temp with a bar graph and blinks when
// the threshold is exceeded.
// =====================================================
void drawTempAlertPage(float tempC) {
  u8g2.clearBuffer();

  // ── Flashing header — inverts every 400 ms ──
  bool blink = ((millis() / 400) % 2 == 0);
  if (blink) {
    u8g2.setDrawColor(1); u8g2.drawBox(0, 0, 128, 14);
    u8g2.setDrawColor(0);
  } else {
    u8g2.setDrawColor(1);
  }
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 11, "HIGH TEMPERATURE!");
  u8g2.setDrawColor(1);

  // ── Large temperature value ──
  u8g2.setFont(u8g2_font_logisoso24_tf);
  char tbuf[10];
  snprintf(tbuf, sizeof(tbuf), "%.1f", tempC);
  int tw = u8g2.getStrWidth(tbuf);
  u8g2.setCursor((128 - tw) / 2, 46);
  u8g2.print(tbuf);

  // Degree-C suffix in smaller font, positioned right of the number
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor((128 + tw) / 2 + 2, 38);
  u8g2.print("o");   // superscript-style degree
  u8g2.setCursor((128 + tw) / 2 + 2, 48);
  u8g2.print("C");

  // ── Threshold bar ──
  // Map tempC into a bar: empty = TEMP_ALERT_THRESHOLD_C - 20,
  //                        full  = TEMP_ALERT_THRESHOLD_C + 20
  float low  = TEMP_ALERT_THRESHOLD_C - 20.0f;
  float high = TEMP_ALERT_THRESHOLD_C + 20.0f;
  float norm = (tempC - low) / (high - low);
  if (norm < 0.0f) norm = 0.0f;
  if (norm > 1.0f) norm = 1.0f;
  int barW = (int)(norm * 124.0f);
  u8g2.drawFrame(2, 52, 124, 10);
  if (barW > 0) u8g2.drawBox(2, 52, barW, 10);

  // Threshold tick mark
  int tickX = (int)(((TEMP_ALERT_THRESHOLD_C - low) / (high - low)) * 124.0f) + 2;
  u8g2.drawLine(tickX, 50, tickX, 63);

  u8g2.sendBuffer();
}
// =====================================================
// BLINDSPOT WARNING SCREEN
//
// Layout (128×64):
//   Top bar  — inverted "BLINDSPOT" header
//   Left half  — shows LEFT side status (distance or CLEAR)
//   Right half — shows RIGHT side status (distance or CLEAR)
//   Centre     — simple top-view car icon
//   Bottom     — animated hazard arrow(s) on the active side(s)
// =====================================================
void drawBlindspotPage(bool bsLeft, bool bsRight,
                       int distLeft, int distRight) {
  u8g2.clearBuffer();

  // ── Flashing inverted header ──────────────────────────────────────
  bool blink = ((millis() / 300) % 2 == 0);
  u8g2.setDrawColor(1);
  if (blink) u8g2.drawBox(0, 0, 128, 13);
  else       u8g2.drawFrame(0, 0, 128, 13);
  u8g2.setDrawColor(blink ? 0 : 1);
  u8g2.setFont(u8g2_font_5x7_tf);
  int hw = u8g2.getStrWidth("!! BLINDSPOT !!");
  u8g2.setCursor((128 - hw) / 2, 10);
  u8g2.print("!! BLINDSPOT !!");
  u8g2.setDrawColor(1);

  // ── Simple top-view car icon (centred) ───────────────────────────
  // Body
  u8g2.drawRFrame(50, 18, 28, 36, 3);
  // Windscreen lines
  u8g2.drawLine(54, 22, 74, 22);
  u8g2.drawLine(54, 48, 74, 48);
  // Wheels (four small filled boxes)
  u8g2.drawBox(47, 20, 4, 7);   // front-left
  u8g2.drawBox(77, 20, 4, 7);   // front-right
  u8g2.drawBox(47, 45, 4, 7);   // rear-left
  u8g2.drawBox(77, 45, 4, 7);   // rear-right

  // ── LEFT side ────────────────────────────────────────────────────
  u8g2.setFont(u8g2_font_5x7_tf);
  if (bsLeft) {
    // Animated filled warning arrow pointing RIGHT (toward the car)
    int arrowX = 2 + ((millis() / 200) % 3) * 3;  // pulses right
    // Arrow body
    u8g2.drawBox(arrowX,      32, 10, 5);
    // Arrow head
    u8g2.drawTriangle(arrowX+10, 28,
                      arrowX+10, 41,
                      arrowX+18, 36);
    // Distance label
    if (distLeft >= 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%dcm", distLeft);
      u8g2.setCursor(2, 60);
      u8g2.print(buf);
    }
  } else {
    // Clear indicator
    u8g2.setCursor(4, 38);
    u8g2.print("CLEAR");
  }

  // ── RIGHT side ───────────────────────────────────────────────────
  if (bsRight) {
    // Animated filled warning arrow pointing LEFT (toward the car)
    int arrowX = 126 - 18 - ((millis() / 200) % 3) * 3;  // pulses left
    // Arrow head
    u8g2.drawTriangle(arrowX,      28,
                      arrowX,      41,
                      arrowX - 8,  36);
    // Arrow body
    u8g2.drawBox(arrowX, 32, 10, 5);
    // Distance label (right-aligned)
    if (distRight >= 0) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%dcm", distRight);
      int dw = u8g2.getStrWidth(buf);
      u8g2.setCursor(126 - dw, 60);
      u8g2.print(buf);
    }
  } else {
    int cw = u8g2.getStrWidth("CLEAR");
    u8g2.setCursor(124 - cw, 38);
    u8g2.print("CLEAR");
  }

  u8g2.sendBuffer();
}