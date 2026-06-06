#ifndef WEBMANAGER_H
#define WEBMANAGER_H

#include <Arduino.h>
#include <FFat.h>     
#include <WiFi.h>          
#include <WebServer.h>     
#include <WiFiClientSecure.h> 

// --- MAGIC SWITCH: Must be defined BEFORE the Firebase include! ---
#define ENABLE_DATABASE

#include <FirebaseClient.h>   
#include "Config.h"
#include "Sensors.h"
#include "SimManager.h"    // gprsConnected(), httpPostGPRS()

extern float internalUsedMB;
extern float internalTotalMB;
extern bool internalMounted;

// Web-injected location — written by /push-location POST from the webapp.
// Exposed here so Sensors.cpp can read them via the extern in Sensors.h.
extern double webInjectedLat;
extern double webInjectedLon;
extern float  webInjectedSpeed;
extern bool   webLocationInjected;

extern String dataBuffer;
extern int bufferCount;

void initWebManager();
void handleWebManager(); 
void logDataToInternal(SensorData d); 
void forceSaveData(); 

void initWebServer();
void handleWebServer();
void uploadToFirebase(SensorData d); 
void pollTelemetryLocation();   // runs on Core 0 via telemetryPollTask — do NOT call from loop()

#endif