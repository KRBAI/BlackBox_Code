#ifndef WEBMANAGER_H
#define WEBMANAGER_H

#include <Arduino.h>
#include <FFat.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>

#define ENABLE_DATABASE
#include <FirebaseClient.h>

#include "Config.h"
#include "Sensors.h"
#include "SimManager.h"

// Internal storage stats (read by Sensors.cpp)
extern float internalUsedMB;
extern float internalTotalMB;
extern bool  internalMounted;

// Web-injected location (from /push-location POST, used when GPS has no fix)
extern double webInjectedLat;
extern double webInjectedLon;
extern float  webInjectedSpeed;
extern bool   webLocationInjected;

// Web-injected blindspot distances in cm.
// Written by /push-blindspot POST from the companion app.
// -1 = no sensor / no reading available.
extern int webBlindLeft;
extern int webBlindRight;

void initWebManager();
void handleWebManager();
void logDataToInternal(const SensorData& d);
void forceSaveData();

void initWebServer();
void handleWebServer();
void uploadToFirebase(const SensorData& d);
void pollTelemetryLocation();
void pollBlindspotData();   // reads /telemetry from Firebase (written by ESP32-C3)

#endif