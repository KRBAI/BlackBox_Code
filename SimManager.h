#ifndef SIMMANAGER_H
#define SIMMANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"
#include "Sensors.h"

struct SimStatus {
  bool registered = false;  // true = home or roaming network
};

extern SemaphoreHandle_t simStatusMutex;

void initSimManager();
SimStatus getSimStatus();
bool syncTimeFromSIM();

enum SosEventType {
  SOS_IMPACT,    
  SOS_ROLLOVER,  
  SOS_MANUAL     
};

// Takes full SensorData struct to build rich location and speed context
void sendLocationSMS(const SensorData& data, SosEventType eventType = SOS_IMPACT);

bool initGPRS();
bool gprsConnected();
int httpPostGPRS(const String& host, const String& path,
                 const String& body, unsigned long timeoutMs = 10000);

#endif