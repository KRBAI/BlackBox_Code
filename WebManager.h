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

extern float internalUsedMB;
extern float internalTotalMB;
extern bool internalMounted;

extern String dataBuffer;
extern int bufferCount;

void initWebManager();
void handleWebManager(); 
void logDataToInternal(SensorData d); 
void forceSaveData(); 

void initWebServer();
void handleWebServer();
void uploadToFirebase(SensorData d); 

#endif