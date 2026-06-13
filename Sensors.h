#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <TinyGPS++.h>
#include "Config.h"

// Software clock — set by SimManager, ticks via millis()
extern unsigned long softClockBase;
extern unsigned long softClockSetAt;
void setSoftClock(unsigned long unixLKT);

// Web-injected location — written by WebManager (/push-location endpoint),
// read by getSensorReadings() and sendLocationSMS() when GPS has no fix.
extern double webInjectedLat;
extern double webInjectedLon;
extern float  webInjectedSpeed;
extern bool   webLocationInjected;

// Web-injected blindspot distances — written by WebManager (/push-blindspot endpoint).
// Values are in cm.  -1 means no sensor / no reading yet.
extern int webBlindLeft;
extern int webBlindRight;

struct SensorData {
  // IMU (Accelerometer & Gyroscope)
  float ax, ay, az;
  float gx, gy, gz;
  float temp;

  // GPS Data
  double lat, lon;
  double alt;
  float  speed;
  int    sats;
  bool   gpsValid;

  // Time & Date
  String dateStr;
  String timeStr;

  // Storage Status
  bool  sdStatus;
  float sdUsedMB;
  float sdTotalMB;

  // Web-injected location (from webapp via /push-location when GPS has no fix)
  double webLat;
  double webLon;
  float  webSpeed;
  bool   webLocationValid;

  // Blindspot sensor distances in cm (-1 = no reading)
  int blindLeft;
  int blindRight;
};

void initSensors();
void calibrateSensors();
SensorData getSensorReadings();

#endif