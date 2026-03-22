#include "Graphics.h"
#include "SimManager.h"

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
void drawClockPage(SensorData data) {
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
void drawAccelPage(SensorData data) {
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
void drawGyroPage(SensorData data) {
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
void drawGPSPage(SensorData data) {
  u8g2.clearBuffer();
  
  // Top Header Bar
  u8g2.setDrawColor(1); u8g2.drawBox(0, 0, 128, 11);
  u8g2.setDrawColor(0); u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.setCursor(2, 8); u8g2.print("SATS:"); u8g2.print(data.sats);
  u8g2.setCursor(84, 8); u8g2.print("GPS NAV");
  u8g2.setDrawColor(1);

  // Center Divider Line
  u8g2.drawLine(64, 12, 64, 64); 

  // LEFT SIDE: Coordinates
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 22, "LATITUDE");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(2, 34); 
  u8g2.print(data.lat, 5); // 5 decimals fits the left screen half nicely

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 48, "LONGITUDE");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(2, 60); 
  u8g2.print(data.lon, 5);

  // RIGHT SIDE: Speedometer
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(74, 22, "SPEED km/h");
  
  u8g2.setFont(u8g2_font_logisoso24_tf); // Big bold font for speed
  int currentSpeed = (int)data.speed;
  if (currentSpeed < 0) currentSpeed = 0; // Prevent negative glitch
  
  // Center the speed number on the right side of the screen
  String spdStr = String(currentSpeed);
  int w = u8g2.getStrWidth(spdStr.c_str());
  u8g2.setCursor(64 + (32 - (w/2)), 55); 
  u8g2.print(spdStr);

  u8g2.sendBuffer();
}

// --- PAGE 4: MEMORY ---
void drawSystemPage(SensorData data) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, "SYSTEM MEMORY"); u8g2.drawLine(0, 10, 128, 10);

  u8g2.setFont(u8g2_font_6x10_tf);
  if (!data.sdStatus) {
    u8g2.drawStr(10, 35, "NO STORAGE!");
  } else {
    u8g2.setCursor(0, 30); u8g2.print("Used: "); u8g2.print(data.sdUsedMB, 2); u8g2.print(" MB");
    u8g2.setCursor(0, 45); u8g2.print("Tot : "); u8g2.print(data.sdTotalMB, 2); u8g2.print(" MB");

    if(data.sdTotalMB > 0) {
        int width = (data.sdUsedMB / data.sdTotalMB) * 128.0;
        if(width < 2 && data.sdUsedMB > 0) width = 2; 
        if(width > 128) width = 128;
        u8g2.drawFrame(0, 52, 128, 10); u8g2.drawBox(0, 52, width, 10);
    }
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

void drawFace(SensorData data, bool isHappy) {
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
// PAGE 5: SIM STATUS — signal strength + network info
// =====================================================
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
// PAGE 5: SIM STATUS — signal + operator
// Time is written to RTC and shown on the clock page.
// =====================================================
// =====================================================
// PAGE 5: SIM STATUS — signal strength + operator
// =====================================================
void drawSimPage(SimStatus& sim) {
  u8g2.clearBuffer();

  // ── Top bar (inverted) ──────────────────────────────
  u8g2.setDrawColor(1);
  u8g2.drawBox(0, 0, 128, 13);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(3, 10, "GSM  NETWORK");
  u8g2.setDrawColor(1);

  // ── Signal bars — large, centred, grow upward from y=50 ──
  // 5 bars, widths 10px, gap 6px, total = 5*10 + 4*6 = 74px → start x=27
  const int barBaseY = 50;
  const int barW = 10, barGap = 6, barStartX = 27;
  for (int b = 0; b < 5; b++) {
    int h  = 7 + b * 7;                     // 7,14,21,28,35 px — steps of 7
    int bx = barStartX + b * (barW + barGap);
    int by = barBaseY - h;
    if (b < sim.bars) {
      u8g2.drawBox(bx, by, barW, h);        // filled = active
    } else {
      u8g2.drawFrame(bx, by, barW, h);      // outline = inactive
    }
  }

  // ── dBm label right of bars ──────────────────────────
  u8g2.setFont(u8g2_font_5x7_tf);
  String dbmStr = (sim.dBm != 0) ? String(sim.dBm) + "dBm" : "---";
  u8g2.setCursor(105, 42);
  u8g2.print(dbmStr);

  // ── Thin divider ─────────────────────────────────────
  u8g2.drawLine(0, 55, 128, 55);

  // ── Operator + status on bottom strip ────────────────
  u8g2.setFont(u8g2_font_6x10_tf);
  String left = sim.operatorName.length() > 0 ? sim.operatorName : "No network";
  u8g2.setCursor(0, 63);
  u8g2.print(left);

  // Status badge — right-aligned
  const char* badge = sim.registered ? "ONLINE" : "SEARCH";
  int bw = u8g2.getStrWidth(badge);
  if (sim.registered) {
    // Inverted badge for online
    u8g2.drawBox(128 - bw - 4, 55, bw + 4, 10);
    u8g2.setDrawColor(0);
    u8g2.setCursor(128 - bw - 2, 63);
    u8g2.print(badge);
    u8g2.setDrawColor(1);
  } else {
    // Outlined badge for searching
    u8g2.drawFrame(128 - bw - 4, 55, bw + 4, 10);
    u8g2.setCursor(128 - bw - 2, 63);
    u8g2.print(badge);
  }

  u8g2.sendBuffer();
}