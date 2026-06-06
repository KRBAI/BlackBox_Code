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
SoundManager soundManager;        

// --- TIMERS ---
unsigned long bootTime          = 0;
unsigned long crashStartTime    = 0;   
unsigned long stateChangeTime   = 0;  
unsigned long lastSOSPress      = 0;     
unsigned long lastMovementTime  = 0;
unsigned long lastSimUpdate     = 0;           
unsigned long rolloverStartTime = 0;   // when rollover mode was entered
unsigned long tempAlertTime     = 0;   // when temp alert mode was entered
const unsigned long SLEEP_TIMEOUT = 300000;   // 5 Minutes

// --- VARIABLES ---
#define USING_TOUCH_MODULE true
volatile bool simInitDone = false; // flag to stop connecting animation task 

enum SystemMode { 
  MODE_FACE, 
  MODE_DASHBOARD,
  MODE_CRASH_DETECTED, 
  MODE_COUNTDOWN, 
  MODE_DRIVE_SAFE, 
  MODE_CALLING,
  MODE_ROLLOVER,      // Sustained rollover detected
  MODE_TEMP_ALERT,    // Over-temperature condition
  MODE_SLEEP                                  
};

SystemMode currentMode = MODE_FACE;
int currentPage = 0; 
const int MAX_PAGES = 5;
bool isPressed = false;
unsigned long pressStartTime = 0;
bool longPressTriggered = false;
int tapCount = 0;
unsigned long lastTapTime = 0;

void audioTask(void * parameter) {
  while(true) {
    soundManager.update(); 
    delay(1); 
  }
}

// Runs on Core 0 while initSimManager() blocks on Core 1 (app core)
// Keeps the display alive with a connecting animation during the ~10-60s SIM boot
void simConnectTask(void * parameter) {
  while (!simInitDone) {
    drawConnectingAnimation();
    delay(180); // ~5.5 fps — smooth but not burning CPU
  }
  vTaskDelete(NULL); // clean self-delete when SIM is ready
}

// Runs permanently on Core 0 after boot — polls SIM status every 10s.
// All atQuery() calls use delay() which would freeze the main loop if called
// directly — running on a separate core eliminates this entirely.
void simStatusTask(void * parameter) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10000)); // wait 10s between polls
    updateSimStatus();
  }
}

// Runs permanently on Core 0 — polls Firebase telemetry/gps every 5s.
// pollTelemetryLocation() does a blocking TLS HTTP GET (200-800ms typical).
// Keeping it off Core 1 prevents the OLED, sensors, and touch input from
// freezing during that request.  Only polls when WiFi is connected.
void telemetryPollTask(void * parameter) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(5000)); // poll every 5 seconds
    if (WiFi.status() == WL_CONNECTED) {
      pollTelemetryLocation();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  // 0. Load persisted settings from NVS flash FIRST
  //    Must be called before initWebManager() or initWebServer()
  //    so all modules get the correct saved values.
  settingsManager.begin();

  if (USING_TOUCH_MODULE) pinMode(TOUCH_PIN, INPUT); 
  pinMode(SOS_PIN, INPUT_PULLUP); 

  // 1. Initialize Storage (Low Power)
  initWebManager(); 

  // 2. Initialize Displays and Sound (Medium Power)
  initDisplay();
  soundManager.init();
  soundManager.startGreeting(); 
  
  // BUZZER FIX: Moved to Core 1, Priority 3 to prevent freezing!
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 3, NULL, 1);

  // 3. Start connecting animation on Core 0, then init SIM on this core
  // The animation task keeps the display alive during the ~10-60s SIM boot sequence
  simInitDone = false;
  xTaskCreatePinnedToCore(simConnectTask, "SimAnim", 4096, NULL, 1, NULL, 0);
  initSimManager();   // blocks here until registered (or timeout)
  initGPRS();         // bring up GPRS bearer now that the module is registered
  simInitDone = true; // signals animation task to stop
  delay(50);          // let the task delete itself cleanly

  // Start permanent SIM status polling task on Core 0
  // This keeps all blocking AT delays off the main loop
  xTaskCreatePinnedToCore(simStatusTask,      "SimStatus",    4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(telemetryPollTask,  "TelePoll",     4096, NULL, 1, NULL, 0);

  initSensors();
  accidentDetector.init(); 

  // 4. Calibrate everything while screen is on
  showCalibratingText();
  calibrateSensors();
  playStartupAnimation(); 

  // 5. TURN ON WIFI LAST (High Power Spike)
  Serial.println(">>> System fully booted. Igniting WiFi Radio... <<<");
  initWebServer(); 
  
  bootTime = millis();
  lastMovementTime = millis(); 
}

void loop() {
  SensorData data = getSensorReadings();
  unsigned long now = millis();

  // --- 1. SLEEP & WAKE LOGIC ---
  if (accidentDetector.isMoving(data)) {
    lastMovementTime = now; 
    if (currentMode == MODE_SLEEP) {
      currentMode = MODE_FACE; 
      soundManager.startGreeting(); 
      Serial.println(">>> WAKING UP: Motion Detected <<<");
    }
  }

  if (currentMode != MODE_SLEEP && (now - lastMovementTime > SLEEP_TIMEOUT)) {
    forceSaveData(); 
    currentMode = MODE_SLEEP;
    u8g2.clearBuffer(); 
    u8g2.sendBuffer();  
    Serial.println(">>> SLEEP MODE: No motion for 5 mins <<<");
  }

  // --- 2. STOP LOGGING IF ASLEEP ---
  if (currentMode == MODE_SLEEP) {
    handleWebServer(); 
    delay(200); 
    return;     
  }

  // --- 3. NORMAL OPERATION ---
  handleWebManager();
  handleWebServer();
  logDataToInternal(data);
  uploadToFirebase(data);
  handleInput();



  // --- 4. MANUAL SOS ---
  if (digitalRead(SOS_PIN) == LOW) {
      if (now - lastSOSPress > 2000) {
          lastSOSPress = now;
          currentMode = MODE_CALLING;
          soundManager.startSiren(); 
          forceSaveData(); 
          sendLocationSMS(data.lat, data.lon, SOS_MANUAL); 
      }
  }

  // --- 5. AUTOMATIC ACCIDENT DETECTION ---
  if (currentMode == MODE_FACE || currentMode == MODE_DASHBOARD) {
      if (accidentDetector.update(data)) {
        currentMode = MODE_CRASH_DETECTED;
        crashStartTime = now;
        soundManager.startSiren(); 
        forceSaveData(); 
      }
  }

  // --- 6. ROLLOVER DETECTION ---
  // Run in normal modes AND during countdown (a rollover mid-slide is still a rollover).
  if (currentMode == MODE_FACE || currentMode == MODE_DASHBOARD ||
      currentMode == MODE_CRASH_DETECTED || currentMode == MODE_COUNTDOWN) {
    TiltEvent tilt = accidentDetector.updateTilt(data);
    if (tilt.rolledOver) {
      currentMode      = MODE_ROLLOVER;
      rolloverStartTime = now;
      soundManager.startSiren();
      forceSaveData();
      sendLocationSMS(data.lat, data.lon, SOS_ROLLOVER);
    }
  }

  // --- 7. TEMPERATURE MONITORING ---
  // Always check temperature regardless of mode (device can overheat while parked).
  {
    TempEvent temp = accidentDetector.updateTemperature(data);
    if (temp.alertTriggered &&
        currentMode != MODE_CALLING &&
        currentMode != MODE_ROLLOVER) {
      currentMode  = MODE_TEMP_ALERT;
      tempAlertTime = now;
      soundManager.startSiren();
    }
  }

  // --- 6. DISPLAY STATE MACHINE ---
  switch(currentMode) {
      case MODE_FACE:
        if (longPressTriggered) drawFace(data, true);
        else drawFace(data, false);
        break;

      case MODE_DASHBOARD:
        switch(currentPage) {
          case 0: drawClockPage(data); break;
          case 1: drawAccelPage(data); break;
          case 2: drawGyroPage(data); break;
          case 3: drawGPSPage(data); break;
          case 4: drawSystemPage(data); break;
          case 5: drawSimPage(getSimStatus()); break;
        }
        break;

      case MODE_CRASH_DETECTED:
        drawCrashPage(accidentDetector.getMaxImpact());
        if (now - crashStartTime > 3000) {
           currentMode = MODE_COUNTDOWN; 
           crashStartTime = now; 
           soundManager.startCountdown();
        }
        break;

      case MODE_COUNTDOWN:
        {
          int elapsed = (now - crashStartTime) / 1000;
          int remaining = 60 - elapsed;
          drawCountdownPage(remaining);
          if (tapCount > 0) {
             tapCount = 0; 
             currentMode = MODE_DRIVE_SAFE; 
             stateChangeTime = now;
             soundManager.stopSiren();
          }
          if (remaining <= 0) {
             currentMode = MODE_CALLING;
             soundManager.startSiren();
             sendLocationSMS(data.lat, data.lon, SOS_IMPACT); 
          }
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
           accidentDetector.reset(); 
           currentMode = MODE_DASHBOARD; 
           tapCount = 0;
           soundManager.stopSiren();
        }
        break;

      // ── Rollover: show angle screen, auto-reset after 10s of normal attitude ──
      case MODE_ROLLOVER:
        {
          TiltEvent tilt = accidentDetector.updateTilt(data);
          drawRolloverPage(tilt.roll, tilt.pitch);
          // Allow manual dismiss with a tap
          if (tapCount > 0) {
            tapCount = 0;
            accidentDetector.resetRollover();
            currentMode = MODE_DASHBOARD;
            soundManager.stopSiren();
          }
          // Auto-clear if the vehicle has returned to normal attitude for 3s
          if (!tilt.inRollover && (now - rolloverStartTime > 3000)) {
            accidentDetector.resetRollover();
            currentMode = MODE_DASHBOARD;
            soundManager.stopSiren();
          }
        }
        break;

      // ── Temp alert: show temperature screen, dismiss with tap or auto-clear ──
      case MODE_TEMP_ALERT:
        {
          TempEvent temp = accidentDetector.updateTemperature(data);
          drawTempAlertPage(temp.tempC);
          // Tap to acknowledge and return to dashboard
          if (tapCount > 0) {
            tapCount = 0;
            currentMode = MODE_DASHBOARD;
            soundManager.stopSiren();
          }
          // Auto-clear after 30s regardless (temp alert is informational, not critical)
          if (now - tempAlertTime > 30000) {
            currentMode = MODE_DASHBOARD;
            soundManager.stopSiren();
          }
        }
        break;
      
      default: break;
  }
}

// --- INPUT HANDLER ---
void handleInput() {
  bool active = false;
  if (USING_TOUCH_MODULE) {
    active = (digitalRead(TOUCH_PIN) == HIGH);
  } else {
    int val = touchRead(TOUCH_PIN);
    active = (val < 50 && val > 0);
  }
  unsigned long now = millis();

  if (active && !isPressed) {
    isPressed = true;
    pressStartTime = now;
  }
  if (isPressed && active) {
    if (now - pressStartTime > 800) {
       longPressTriggered = true;
       if(currentMode != MODE_FACE && currentMode != MODE_DASHBOARD) return; 
    }
  }
  if (!active && isPressed) {
    isPressed = false;
    if (longPressTriggered) {
      longPressTriggered = false;
    } else {
      if (now - pressStartTime > 50) { 
        tapCount++;
        lastTapTime = now;
      }
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
         currentMode = MODE_DASHBOARD;
         currentPage = 0;
         longPressTriggered = false; // clear happy-face state on mode switch
       } else if (currentMode == MODE_DASHBOARD) {
         currentMode = MODE_FACE;
         longPressTriggered = false;
       }
    }
    if (currentMode == MODE_FACE || currentMode == MODE_DASHBOARD) {
      tapCount = 0; 
    }
  }
}