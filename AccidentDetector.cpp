#include "AccidentDetector.h"

void AccidentDetector::init() {
  lastCrashTime = 0;
  maxImpact     = 0;
  inCrashState  = false;
  prevAx = 0; prevAy = 0; prevAz = 0;
  prevValid     = false;
  tiltStartTime = 0;
  tiltActive    = false;
  inRollover    = false;
  tempOverTime  = 0;
  lastTempAlert = 0;
  tempOverActive = false;
}

bool AccidentDetector::update(SensorData data) {
  float gX = data.ax / 9.81f;
  float gY = data.ay / 9.81f;
  float gZ = data.az / 9.81f;
  float totalG = sqrtf(gX*gX + gY*gY + gZ*gZ);

  if (millis() - lastCrashTime < CRASH_COOLDOWN_MS) return false;

  if (totalG > IMPACT_THRESHOLD_G) {
    lastCrashTime = millis();
    maxImpact = totalG;
    inCrashState = true;
    return true;
  }
  return false;
}

// ── ROLLOVER DETECTION ─────────────────────────────────────────────────────
//
// Strategy: derive roll and pitch from the gravity vector measured by the
// accelerometer.  When the vehicle is stationary or moving at constant speed,
// the accelerometer measures only gravitational acceleration (≈9.81 m/s²
// pointing "down").  By computing the angle of that vector relative to each
// horizontal axis we get roll and pitch.
//
// Roll  = atan2(ay, az)   — rotation around the front-back axis
// Pitch = atan2(-ax, sqrt(ay²+az²))  — rotation around the side axis
//
// A |angle| > ROLLOVER_ANGLE_DEG sustained for ROLLOVER_SUSTAIN_MS triggers
// a rollover event.  Hard cornering creates a brief large lateral-G spike but
// it is NOT sustained — the sustain window is the key false-positive filter.
//
// Limitation: during heavy braking / acceleration the gravity vector is
// contaminated by linear acceleration, so readings can be noisy.  That is
// acceptable here because rollovers involve sustained attitude change, not
// momentary spikes.
TiltEvent AccidentDetector::updateTilt(SensorData data) {
  TiltEvent ev;

  // Compute roll and pitch in degrees from the calibrated accelerometer
  ev.roll  = atan2f(data.ay, data.az) * (180.0f / M_PI);
  ev.pitch = atan2f(-data.ax, sqrtf(data.ay * data.ay + data.az * data.az))
             * (180.0f / M_PI);

  // Is either angle beyond the rollover threshold?
  bool overAngle = (fabsf(ev.roll) > ROLLOVER_ANGLE_DEG ||
                    fabsf(ev.pitch) > ROLLOVER_ANGLE_DEG);

  unsigned long now = millis();

  if (overAngle) {
    if (!tiltActive) {
      // First frame over threshold — start the sustain timer
      tiltActive    = true;
      tiltStartTime = now;
    }
    // Check if the angle has been sustained long enough
    if (!inRollover && (now - tiltStartTime >= ROLLOVER_SUSTAIN_MS)) {
      inRollover    = true;
      ev.rolledOver = true;   // Rising-edge trigger — fires exactly once
    }
  } else {
    // Angle returned to normal — reset sustain state but keep inRollover
    // until resetRollover() is explicitly called (after SMS / display handled)
    tiltActive    = false;
    tiltStartTime = 0;
  }

  ev.inRollover = inRollover;
  if (!inRollover) ev.rolledOver = false;
  return ev;
}

void AccidentDetector::resetRollover() {
  inRollover    = false;
  tiltActive    = false;
  tiltStartTime = 0;
}

// ── TEMPERATURE MONITORING ─────────────────────────────────────────────────
//
// The MPU6050 has an internal temperature sensor with ±3°C accuracy.
// It measures the die temperature, which runs roughly 2-4°C above ambient.
// We apply a sustain window to ignore single noisy samples, and a cooldown
// to avoid re-triggering every loop while the device stays hot.
TempEvent AccidentDetector::updateTemperature(SensorData data) {
  TempEvent ev;
  ev.tempC          = data.temp;
  ev.alertTriggered = false;
  ev.overThreshold  = (data.temp >= TEMP_ALERT_THRESHOLD_C);

  unsigned long now = millis();

  if (ev.overThreshold) {
    if (!tempOverActive) {
      tempOverActive = true;
      tempOverTime   = now;
    }
    // Sustain window passed AND cooldown since last alert has elapsed?
    bool sustained = (now - tempOverTime  >= TEMP_ALERT_SUSTAIN_MS);
    bool cooled    = (now - lastTempAlert >= TEMP_ALERT_COOLDOWN_MS);
    if (sustained && cooled) {
      ev.alertTriggered = true;
      lastTempAlert     = now;
    }
  } else {
    // Temperature dropped below threshold — reset sustain timer
    tempOverActive = false;
    tempOverTime   = 0;
  }
  return ev;
}

bool AccidentDetector::isMoving(SensorData data) {
  if (!prevValid) {
    prevAx = data.ax; prevAy = data.ay; prevAz = data.az;
    prevValid = true;
    return false;
  }
  float deltaX = fabsf(data.ax - prevAx);
  float deltaY = fabsf(data.ay - prevAy);
  float deltaZ = fabsf(data.az - prevAz);
  prevAx = data.ax; prevAy = data.ay; prevAz = data.az;
  return (deltaX > 0.08f || deltaY > 0.08f || deltaZ > 0.08f);
}

float AccidentDetector::getMaxImpact() { return maxImpact; }

void AccidentDetector::reset() {
  inCrashState = false;
  maxImpact    = 0;
}