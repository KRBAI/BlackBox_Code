#ifndef ACCIDENT_DETECTOR_H
#define ACCIDENT_DETECTOR_H

#include <math.h>
#include "Sensors.h"
#include "Config.h"

// ── PROTOTYPE thresholds — easy to trigger on a desk ──────────────
// Impact : 1.2 G — a moderate desk tap will fire it
//          (vehicle install: raise back to 3.0-4.0 G)
// Cooldown: 3 s so you can re-trigger quickly while testing
//          (vehicle install: 5000 ms)
// Rollover: 30 deg tilt sustained 800 ms triggers it by hand
//          (vehicle install: 60 deg / 1500 ms — set in Config.h)
#define IMPACT_THRESHOLD_G   2.0f
#define CRASH_COOLDOWN_MS    3000
#define SLEEP_THRESHOLD      0.15

// Override Config.h rollover values for prototype testing
#undef  ROLLOVER_ANGLE_DEG
#define ROLLOVER_ANGLE_DEG   30.0f
#undef  ROLLOVER_SUSTAIN_MS
#define ROLLOVER_SUSTAIN_MS  800

// ── Rollover event ──────────────────────────────────────────────────────────
struct TiltEvent {
  float roll;         
  float pitch;        
  bool  rolledOver;   
  bool  inRollover;   
};

// ── Temperature event ───────────────────────────────────────────────────────
struct TempEvent {
  float tempC;           
  bool  alertTriggered;  
  bool  overThreshold;   
};

class AccidentDetector {
  public:
    void init();
    bool update(const SensorData& data);       
    TiltEvent updateTilt(const SensorData& data);          
    TempEvent updateTemperature(const SensorData& data);   
    bool isMoving(const SensorData& data);
    float getMaxImpact();
    void reset();
    void resetRollover();   

  private:
    unsigned long lastCrashTime = 0;
    float maxImpact = 0;
    bool inCrashState = false;

    float prevAx = 0, prevAy = 0, prevAz = 0;
    bool  prevValid = false;

    unsigned long tiltStartTime  = 0;   
    bool          tiltActive     = 0;   
    bool          inRollover     = false; 

    unsigned long tempOverTime   = 0;   
    unsigned long lastTempAlert  = 0;   
    bool          tempOverActive = false; 
};

#endif