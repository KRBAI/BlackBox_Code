#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <Arduino.h>

// Using Pin 7 (Standard Buzzer)
#define BUZZER_PIN 7

enum SoundState {
  SILENT,
  GREETING,
  SIREN,
  COUNTDOWN
};

class SoundManager {
  public:
    void init();       
    void startGreeting();  // Startup "System Ready" chirp
    void startSiren();     // Futuristic Alarm
    void startCountdown(); // Heartbeat/Geiger ticks
    void stopSiren();      // Silence
    void update();     

  private:
    SoundState currentState = SILENT;
    unsigned long stateStartTime = 0;
    unsigned long lastUpdate = 0;
    int sweepFreq = 0;
    bool sweepUp = true;
};

#endif