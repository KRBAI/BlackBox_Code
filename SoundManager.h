#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <Arduino.h>

#define BUZZER_PIN 7

enum SoundState {
  SILENT,
  GREETING,
  SIREN,
  COUNTDOWN,
  BLINDSPOT_BEEP   // Short chirp repeated every 500 ms — gentler than siren
};

class SoundManager {
  public:
    void init();
    void startGreeting();      // Startup "System Ready" chirp
    void startSiren();         // Futuristic warble — emergency events
    void startCountdown();     // Radar blip — crash countdown ticks
    void startBlindspotBeep(); // Short double-chirp — proximity warning
    void stopSiren();          // Silence all sounds
    void update();             // Call every ms from audioTask

  private:
    SoundState    currentState  = SILENT;
    unsigned long stateStartTime = 0;
    unsigned long lastUpdate    = 0;
    int           sweepFreq     = 0;
    bool          sweepUp       = true;
};

#endif