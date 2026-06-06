#ifndef ACCIDENT_DETECTOR_H
#define ACCIDENT_DETECTOR_H

#include <math.h>
#include "Sensors.h"
#include "Config.h"

#define IMPACT_THRESHOLD_G 3.0  
#define CRASH_COOLDOWN_MS  5000 
#define SLEEP_THRESHOLD    0.15

// ── Rollover event ──────────────────────────────────────────────────────────
// Returned by updateTilt() every loop iteration.
// rolledOver is true only on the frame where the sustained-angle threshold
// is first crossed; it resets when the vehicle returns to normal attitude.
struct TiltEvent {
  float roll;         // degrees, –180 to +180
  float pitch;        // degrees, –90 to +90
  bool  rolledOver;   // true = newly-triggered rollover this frame
  bool  inRollover;   // true = currently in sustained rollover state
};

// ── Temperature event ───────────────────────────────────────────────────────
// Returned by updateTemperature() every loop iteration.
// alertTriggered is true only on the frame where the threshold is
// first exceeded after the sustain window; resets after cooldown.
struct TempEvent {
  float tempC;           // current MPU6050 die temperature in °C
  bool  alertTriggered;  // true = newly-triggered alert this frame
  bool  overThreshold;   // true = currently above threshold
};

class AccidentDetector {
  public:
    void init();
    bool update(SensorData data);       // Linear impact detection
    TiltEvent updateTilt(SensorData data);          // Rollover detection
    TempEvent updateTemperature(SensorData data);   // Thermal monitoring
    bool isMoving(SensorData data);
    float getMaxImpact();
    void reset();
    void resetRollover();   // call after rollover event is handled

  private:
    // ── Impact ──
    unsigned long lastCrashTime = 0;
    float maxImpact = 0;
    bool inCrashState = false;

    // ── Motion (delta-G) ──
    float prevAx = 0, prevAy = 0, prevAz = 0;
    bool  prevValid = false;

    // ── Rollover ──
    unsigned long tiltStartTime  = 0;   // when angle first exceeded threshold
    bool          tiltActive     = 0;   // currently above threshold
    bool          inRollover     = false; // sustained rollover in progress

    // ── Temperature ──
    unsigned long tempOverTime   = 0;   // when temp first exceeded threshold
    unsigned long lastTempAlert  = 0;   // millis() of last alert fire
    bool          tempOverActive = false;
};

#endif