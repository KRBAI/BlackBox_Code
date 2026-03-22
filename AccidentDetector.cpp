#include "AccidentDetector.h"

void AccidentDetector::init() {
  lastCrashTime = 0;
  maxImpact = 0;
  inCrashState = false;
}

bool AccidentDetector::update(SensorData data) {
  float gX = data.ax / 9.81;
  float gY = data.ay / 9.81;
  float gZ = data.az / 9.81;

  float totalG = sqrt((gX * gX) + (gY * gY) + (gZ * gZ));

  if (millis() - lastCrashTime < CRASH_COOLDOWN_MS) {
    return false; 
  }

  if (totalG > IMPACT_THRESHOLD_G) {
    lastCrashTime = millis();
    maxImpact = totalG;
    inCrashState = true;
    return true; 
  }

  return false;
}

// --- UPDATED: Motion Detection using Delta-G (Ignores Gravity) ---
bool AccidentDetector::isMoving(SensorData data) {
  // Static variables remember the sensor's position from the previous millisecond
  static float lastAx = data.ax;
  static float lastAy = data.ay;
  static float lastAz = data.az;
  
  // Calculate the difference (vibration/jerk) between now and a moment ago
  float deltaX = abs(data.ax - lastAx);
  float deltaY = abs(data.ay - lastAy);
  float deltaZ = abs(data.az - lastAz);
  
  // Update the memory for the next loop
  lastAx = data.ax;
  lastAy = data.ay;
  lastAz = data.az;
  
  // 0.08 G is a highly sensitive vibration threshold. 
  // It will catch an engine rumble but ignore static gravity tilts.
  if (deltaX > 0.08 || deltaY > 0.08 || deltaZ > 0.08) {
    return true; 
  }
  return false;
}

float AccidentDetector::getMaxImpact() {
  return maxImpact;
}

void AccidentDetector::reset() {
  inCrashState = false;
  maxImpact = 0;
}