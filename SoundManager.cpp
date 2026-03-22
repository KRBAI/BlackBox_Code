#include "SoundManager.h"

void SoundManager::init() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[Buzzer] Initialized on Pin 7");
}

void SoundManager::startGreeting() {
  currentState = GREETING;
  stateStartTime = millis();
  Serial.println("[Buzzer] Greeting");
}

void SoundManager::startSiren() {
  if (currentState == SIREN) return; // Already playing
  currentState = SIREN;
  sweepFreq = 800;
  Serial.println("[Buzzer] Siren START");
}

void SoundManager::startCountdown() {
  if (currentState == COUNTDOWN) return;
  currentState = COUNTDOWN;
  Serial.println("[Buzzer] Countdown Mode");
}

void SoundManager::stopSiren() {
  currentState = SILENT;
  noTone(BUZZER_PIN);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[Buzzer] STOP");
}

void SoundManager::update() {
  unsigned long now = millis();

  switch (currentState) {
    case SILENT:
      noTone(BUZZER_PIN);
      break;

    // 1. GREETING: "Sci-Fi Power On" (Fast Sweep Up)
    case GREETING:
      if (now - stateStartTime < 150) {
        tone(BUZZER_PIN, 1200); // Low Tone
      } else if (now - stateStartTime < 300) {
        tone(BUZZER_PIN, 2000); // High Tone
      } else if (now - stateStartTime < 450) {
        tone(BUZZER_PIN, 2500); // Higher Tone
      } else {
        currentState = SILENT;  // Done
        noTone(BUZZER_PIN);
      }
      break;

    // 2. SIREN: "Futuristic Warble" (Fast oscillation)
    case SIREN:
      if (now - lastUpdate > 15) { // Update every 15ms
        lastUpdate = now;
        
        // Fast sweep between 800Hz and 1500Hz
        if (sweepUp) {
          sweepFreq += 50;
          if (sweepFreq >= 1500) sweepUp = false;
        } else {
          sweepFreq -= 50;
          if (sweepFreq <= 800) sweepUp = true;
        }
        tone(BUZZER_PIN, sweepFreq);
      }
      break;

    // 3. COUNTDOWN: "Radar Blip" (Short tick every second)
    case COUNTDOWN:
      // We rely on the millisecond clock to sync perfectly with seconds
      // Blip for the first 50ms of every second
      int ms = now % 1000;
      if (ms < 50) {
        tone(BUZZER_PIN, 3000); // Sharp, high frequency tick
      } else {
        noTone(BUZZER_PIN);     // Silence for the rest of the second
      }
      break;
  }
}