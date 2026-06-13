#include "Config.h"
#include "Sensors.h"
#include "Graphics.h"
#include "WebManager.h"
#include "SimManager.h"
#include "AccidentDetector.h"
#include "SoundManager.h"
#include "SettingsManager.h"

// --- OBJECTS ---
AccidentDetector accidentDetector;
SoundManager     soundManager;

// --- TIMERS ---
unsigned long crashStartTime    = 0;
unsigned long stateChangeTime   = 0;
unsigned long lastSOSPress      = 0;
unsigned long lastMovementTime  = 0;
unsigned long rolloverStartTime = 0;
unsigned long crashBuzzerStop   = 0;  // when to silence impact buzzer
unsigned long tempAlertTime     = 0;
unsigned long sosTriggerTime    = 0;
const unsigned long SLEEP_TIMEOUT = 300000;   // 5 minutes

// --- VARIABLES ---
#define USING_TOUCH_MODULE true
volatile bool simInitDone = false;

enum SystemMode {
  MODE_FACE,
  MODE_DASHBOARD,
  MODE_CRASH_DETECTED,
  MODE_COUNTDOWN,
  MODE_DRIVE_SAFE,
  MODE_MANUAL_WARNING,
  MODE_CALLING,
  MODE_ROLLOVER,
  MODE_TEMP_ALERT,
  MODE_BLINDSPOT,
  MODE_SLEEP
};

SystemMode    currentMode = MODE_FACE;
SystemMode    modeBeforeBS = MODE_FACE;  // mode to restore after blindspot clears
int           currentPage = 0;
const int     MAX_PAGES   = 5;
bool          isPressed   = false;
unsigned long pressStartTime  = 0;
bool          longPressTriggered = false;
int           tapCount    = 0;
unsigned long lastTapTime = 0;

// ─────────────────────────────────────────────────────────────────────────────
// TASKS
// ─────────────────────────────────────────────────────────────────────────────

// Buzzer update — Core 1, high priority so the sweep stays smooth
void audioTask(void * parameter) {
  for (;;) { soundManager.update(); delay(1); }
}

// Animated SIM connecting screen — runs on Core 0 during initSimManager() boot
void simConnectTask(void * parameter) {
  while (!simInitDone) { drawConnectingAnimation(); delay(180); }
  vTaskDelete(NULL);
}

// Firebase telemetry poll task — Core 0
// Polls blindspot data every 500 ms (near real-time for the driver).
// Polls GPS location every 5 s (slower — only needed as GPS fallback).
void telemetryPollTask(void * parameter) {
  unsigned long lastLocationPoll = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500));          // tick every 500 ms
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
      pollBlindspotData();                   // always — 500 ms cadence
      if (now - lastLocationPoll >= 5000) {  // every 5 s
        pollTelemetryLocation();
        lastLocationPoll = now;
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  settingsManager.begin();

  if (USING_TOUCH_MODULE) pinMode(TOUCH_PIN, INPUT);
  pinMode(SOS_PIN, INPUT_PULLUP);

  initWebManager();   // mount FFat storage
  initDisplay();
  soundManager.init();
  soundManager.startGreeting();

  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 3, NULL, 1);

  // SIM init (blocking ~10–60 s) — animate display on Core 0 while we wait
  simInitDone = false;
  xTaskCreatePinnedToCore(simConnectTask, "SimAnim", 4096, NULL, 1, NULL, 0);
  initSimManager();
  initGPRS();
  simInitDone = true;
  delay(50);

  xTaskCreatePinnedToCore(telemetryPollTask, "TelePoll", 6144, NULL, 1, NULL, 1);

  initSensors();
  accidentDetector.init();

  showCalibratingText();
  calibrateSensors();
  playStartupAnimation();

  Serial.println(">>> System booted. Starting WiFi... <<<");
  initWebServer();   // connect to router + start HTTP server

  lastMovementTime = millis();
}

// ─────────────────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  SensorData    data = getSensorReadings();
  unsigned long now  = millis();

  // ── 1. SLEEP & WAKE ──────────────────────────────────────────────
  if (accidentDetector.isMoving(data)) {
    lastMovementTime = now;
    if (currentMode == MODE_SLEEP) {
      currentMode = MODE_FACE;
      soundManager.startGreeting();
      Serial.println(">>> WAKE: motion detected <<<");
    }
  }
  if (currentMode != MODE_SLEEP && (now - lastMovementTime > SLEEP_TIMEOUT)) {
    forceSaveData();
    currentMode = MODE_SLEEP;
    u8g2.clearBuffer(); u8g2.sendBuffer();
    Serial.println(">>> SLEEP: no motion for 5 min <<<");
  }
  if (currentMode == MODE_SLEEP) {
    handleWebServer();
    delay(200);
    return;
  }

  // ── 2. HOUSEKEEPING ──────────────────────────────────────────────
  handleWebManager();
  handleWebServer();
  logDataToInternal(data);
  uploadToFirebase(data);
  handleInput();

  // ── 3. BLINDSPOT MONITORING ──────────────────────────────────────
  bool bsLeft  = (data.blindLeft  >= 0);
  bool bsRight = (data.blindRight >= 0);

  if (currentMode == MODE_FACE     ||
      currentMode == MODE_DASHBOARD ||
      currentMode == MODE_BLINDSPOT) {

    if (bsLeft || bsRight) {
      if (currentMode != MODE_BLINDSPOT) {
        modeBeforeBS = currentMode;          
        currentMode  = MODE_BLINDSPOT;
        soundManager.startBlindspotBeep();
      }
    } else if (currentMode == MODE_BLINDSPOT) {
      currentMode = modeBeforeBS;
      soundManager.stopSiren();
    }
  }

  // ── 4. MANUAL SOS ────────────────────────────────────────────────
  if (digitalRead(SOS_PIN) == LOW) {
    if (now - lastSOSPress > 2000 &&
        currentMode != MODE_MANUAL_WARNING &&
        currentMode != MODE_CALLING) {
      lastSOSPress   = now;
      sosTriggerTime = now;
      currentMode    = MODE_MANUAL_WARNING;
      soundManager.startSiren();
      forceSaveData();
    }
  }

  // ── 5. IMPACT DETECTION ──────────────────────────────────────────
  if (currentMode == MODE_FACE || currentMode == MODE_DASHBOARD) {
    if (accidentDetector.update(data)) {
      currentMode      = MODE_CRASH_DETECTED;
      crashStartTime   = now;
      crashBuzzerStop  = now + 3000;   // buzzer on for 3 s only
      tapCount         = 0;            // discard any pending taps
      soundManager.startSiren();
      forceSaveData();
    }
  }

  // ── 5b. AUTO-STOP IMPACT BUZZER after 3 s ──────────────────────
  if ((currentMode == MODE_CRASH_DETECTED || currentMode == MODE_COUNTDOWN) &&
      crashBuzzerStop > 0 && now >= crashBuzzerStop) {
    soundManager.stopSiren();
    crashBuzzerStop = 0;   // don't fire again
  }

  // ── 6. ROLLOVER DETECTION ────────────────────────────────────────
  if (currentMode == MODE_FACE     ||
      currentMode == MODE_DASHBOARD ||
      currentMode == MODE_CRASH_DETECTED ||
      currentMode == MODE_COUNTDOWN) {
    TiltEvent tilt = accidentDetector.updateTilt(data);
    if (tilt.rolledOver) {
      currentMode          = MODE_ROLLOVER;
      rolloverStartTime    = now;
      tapCount             = 0;            
      soundManager.startSiren();           
      forceSaveData();
    }
  }

  // ── 7. TEMPERATURE MONITORING ────────────────────────────────────
  {
    TempEvent temp = accidentDetector.updateTemperature(data);
    if (temp.alertTriggered &&
        currentMode != MODE_CALLING &&
        currentMode != MODE_ROLLOVER) {
      currentMode   = MODE_TEMP_ALERT;
      tempAlertTime = now;
      soundManager.startSiren();
    }
  }

  // ── 8. DISPLAY STATE MACHINE ─────────────────────────────────────
  switch (currentMode) {

    case MODE_FACE:
      drawFace(data, longPressTriggered);
      break;

    case MODE_DASHBOARD:
      switch (currentPage) {
        case 0: drawClockPage(data);         break;
        case 1: drawAccelPage(data);         break;
        case 2: drawGyroPage(data);          break;
        case 3: drawGPSPage(data);           break;
        case 4: drawSystemPage(data);        break;
        case 5: drawSimPage(getSimStatus()); break;
      }
      break;

    case MODE_BLINDSPOT:
      {
        bool l = (data.blindLeft >= 0 && data.blindLeft < 50);
        bool r = (data.blindRight >= 0 && data.blindRight < 50);
        drawBlindspotPage(l, r, data.blindLeft, data.blindRight);
        
        if (!l && !r) {
          currentMode = MODE_DASHBOARD;
          soundManager.stopSiren();
        } else if (tapCount > 0) {
          tapCount    = 0;
          currentMode = modeBeforeBS;
          soundManager.stopSiren();
        }
      }
      break;

    case MODE_CRASH_DETECTED:
      drawCrashPage(accidentDetector.getMaxImpact());
      if (now - crashStartTime > 3000) {
        currentMode    = MODE_COUNTDOWN;
        crashStartTime = now;
        soundManager.stopSiren();        
        soundManager.startCountdown();
      }
      break;

    case MODE_COUNTDOWN: 
      {
        int remaining = 60 - (int)((now - crashStartTime) / 1000);
        drawCountdownPage(remaining);
        
        if (tapCount > 0) {
          tapCount        = 0;
          currentMode     = MODE_DRIVE_SAFE;
          stateChangeTime = now;
          soundManager.stopSiren();   
        }
        if (remaining <= 0) {
          soundManager.stopSiren();
          drawSendingPage();
          sendLocationSMS(data, SOS_IMPACT);
          currentMode = MODE_CALLING;
        }
      }
      break;

    case MODE_MANUAL_WARNING:
      drawSendingPage();
      if (now - sosTriggerTime > 3000) {
        soundManager.stopSiren();
        drawSendingPage();
        sendLocationSMS(data, SOS_MANUAL);
        currentMode = MODE_CALLING;
        tapCount    = 0;
      }
      break;

    case MODE_DRIVE_SAFE:
      drawDriveSafePage();
      if (now - stateChangeTime > 3000) {
        currentMode = MODE_DASHBOARD;
        accidentDetector.reset();
      }
      break;

    case MODE_CALLING:
      drawCallingPage();
      if (tapCount > 0) {
        tapCount    = 0;
        accidentDetector.reset();
        currentMode = MODE_DASHBOARD;
        soundManager.stopSiren();
      }
      break;

    case MODE_ROLLOVER: 
      {
        TiltEvent tilt = accidentDetector.updateTilt(data);
        drawRolloverPage(tilt.roll, tilt.pitch); 
        
        // Allow the driver to tap to cancel the alarm during the 3 seconds
        if (tapCount > 0) {
          tapCount = 0;
          accidentDetector.resetRollover();
          soundManager.stopSiren();
          currentMode = MODE_DASHBOARD;
        }
        // After 3 seconds have passed...
        else if (now - rolloverStartTime > 3000) {
          soundManager.stopSiren();            
          drawSendingPage();                   
          sendLocationSMS(data, SOS_ROLLOVER); 
          accidentDetector.resetRollover();    
          currentMode = MODE_CALLING;          
          tapCount = 0;
        }
      }
      break;

    case MODE_TEMP_ALERT: 
      {
        TempEvent temp = accidentDetector.updateTemperature(data);
        drawTempAlertPage(temp.tempC);
        if (tapCount > 0) {
          tapCount    = 0;
          currentMode = MODE_DASHBOARD;
          soundManager.stopSiren();
        }
        if (now - tempAlertTime > 30000) {
          currentMode = MODE_DASHBOARD;
          soundManager.stopSiren();
        }
      }
      break;

    default: break;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// INPUT HANDLER
// ─────────────────────────────────────────────────────────────────────────────
void handleInput() {
  bool active = false;
  if (USING_TOUCH_MODULE) active = (digitalRead(TOUCH_PIN) == HIGH);
  else { int v = touchRead(TOUCH_PIN); active = (v < 50 && v > 0); }

  unsigned long now = millis();

  if (active && !isPressed) { isPressed = true; pressStartTime = now; }

  if (isPressed && active) {
    if (now - pressStartTime > 800) {
      if (currentMode == MODE_FACE || currentMode == MODE_DASHBOARD) {
        longPressTriggered = true;
      }
    }
  }

  if (!active && isPressed) {
    isPressed = false;
    if (longPressTriggered) {
      longPressTriggered = false;
    } else {
      if (now - pressStartTime > 50) { tapCount++; lastTapTime = now; }
    }
  }

  if (tapCount > 0 && (now - lastTapTime > 300)) {
    if (tapCount == 1) {
      if (currentMode == MODE_DASHBOARD) {
        currentPage++;
        if (currentPage > MAX_PAGES) currentPage = 0;
      }
    } else if (tapCount >= 2) {
      if (currentMode == MODE_FACE) {
        currentMode = MODE_DASHBOARD; currentPage = 0; longPressTriggered = false;
      } else if (currentMode == MODE_DASHBOARD) {
        currentMode = MODE_FACE; longPressTriggered = false;
      }
    }
    if (currentMode == MODE_FACE || currentMode == MODE_DASHBOARD) tapCount = 0;
  }
}