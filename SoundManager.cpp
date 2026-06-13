#include "SoundManager.h"

void SoundManager::init() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[Buzzer] Initialized on Pin 7");
}

void SoundManager::startGreeting() {
  currentState   = GREETING;
  stateStartTime = millis();
  Serial.println("[Buzzer] Greeting");
}

void SoundManager::startSiren() {
  if (currentState == SIREN) return;
  currentState = SIREN;
  sweepFreq    = 800;
  Serial.println("[Buzzer] Siren START");
}

void SoundManager::startCountdown() {
  if (currentState == COUNTDOWN) return;
  currentState = COUNTDOWN;
  Serial.println("[Buzzer] Countdown Mode");
}

void SoundManager::startBlindspotBeep() {
  if (currentState == BLINDSPOT_BEEP) return;
  currentState = BLINDSPOT_BEEP;
  Serial.println("[Buzzer] Blindspot Beep START");
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

    // ── SILENT ─────────────────────────────────────────────────────────
    case SILENT:
      noTone(BUZZER_PIN);
      break;

    // ── GREETING: fast sci-fi sweep up ─────────────────────────────────
    case GREETING:
      if      (now - stateStartTime < 150) tone(BUZZER_PIN, 1200);
      else if (now - stateStartTime < 300) tone(BUZZER_PIN, 2000);
      else if (now - stateStartTime < 450) tone(BUZZER_PIN, 2500);
      else { currentState = SILENT; noTone(BUZZER_PIN); }
      break;

    // ── SIREN: futuristic warble 800–1500 Hz ───────────────────────────
    case SIREN:
      if (now - lastUpdate > 15) {
        lastUpdate = now;
        if (sweepUp) { sweepFreq += 50; if (sweepFreq >= 1500) sweepUp = false; }
        else         { sweepFreq -= 50; if (sweepFreq <= 800)  sweepUp = true;  }
        tone(BUZZER_PIN, sweepFreq);
      }
      break;

    // ── COUNTDOWN: sharp 50 ms tick every second ───────────────────────
    case COUNTDOWN:
      if (now % 1000 < 50) tone(BUZZER_PIN, 3000);
      else                 noTone(BUZZER_PIN);
      break;

    // ── BLINDSPOT: two short chirps every 600 ms ───────────────────────
    // Pattern (within a 600 ms window):
    //   0–60 ms   → beep 1  (2200 Hz, friendly alert tone)
    //   60–120 ms → silence
    //   120–180 ms→ beep 2
    //   180–600 ms→ silence
    case BLINDSPOT_BEEP: {
      int phase = now % 600;
      if      (phase < 60)               tone(BUZZER_PIN, 2200);
      else if (phase >= 60  && phase < 120) noTone(BUZZER_PIN);
      else if (phase >= 120 && phase < 180) tone(BUZZER_PIN, 2200);
      else                               noTone(BUZZER_PIN);
      break;
    }
  }
}