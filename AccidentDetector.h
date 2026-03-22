#ifndef ACCIDENT_DETECTOR_H
#define ACCIDENT_DETECTOR_H

#include "Sensors.h"
#include "Config.h"

#define IMPACT_THRESHOLD_G 3.0  
#define CRASH_COOLDOWN_MS  5000 
#define SLEEP_THRESHOLD    0.15  // Sensitivity for movement (Adjust if needed)

class AccidentDetector {
  public:
    void init();
    bool update(SensorData data); 
    bool isMoving(SensorData data); // <--- NEW: Detection for activity
    float getMaxImpact();         
    void reset();                 

  private:
    unsigned long lastCrashTime = 0;
    float maxImpact = 0;
    bool inCrashState = false;
};

#endif