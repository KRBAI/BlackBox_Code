#ifndef SIMMANAGER_H
#define SIMMANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.h"

struct SimStatus {
  bool    registered   = false;  // true = home or roaming network
  int     rssi         = 0;      // 0-31 raw CSQ value (99 = unknown)
  int     dBm          = 0;      // dBm = -113 + (rssi * 2)
  int     bars         = 0;      // 0-5 visual bars for display
  String  operatorName = "---";  // e.g. "Mobitel"
  String  lastRTCSync  = "Never";// timestamp of last successful RTC sync
};

// Mutex protecting simStatus across Core 0 (writer) and Core 1 (reader).
// Always acquire before reading or writing simStatus fields.
extern SemaphoreHandle_t simStatusMutex;

// Initialize SIM800L on UART2, wait for network registration
void initSimManager();

// Refresh signal strength, operator name, and registration status.
// Call from main loop every ~10s — fast, non-blocking (~3s total AT time).
void updateSimStatus();

// Returns a mutex-protected snapshot copy of the current SIM status.
// Safe to call from any core or task.
SimStatus getSimStatus();

// Sync the DS3231 RTC from network time.
// Called automatically on boot after registration.
// Returns true on success.
bool syncTimeFromSIM();

// Type of emergency event — determines the SMS message header and detail
enum SosEventType {
  SOS_IMPACT,    // Linear crash (original behaviour)
  SOS_ROLLOVER,  // Sustained angle rollover
  SOS_MANUAL     // Driver-triggered SOS button
};

// Send SOS SMS with a Google Maps link to the configured phone number
void sendLocationSMS(float lat, float lon, SosEventType eventType = SOS_IMPACT);

// ── GPRS (cellular data) ────────────────────────────────────────────────────
// Bring up the GPRS bearer using the APN stored in SettingsManager.
// Call once after initSimManager() confirms network registration.
// Returns true if the module obtained an IP address.
bool initGPRS();

// Returns true if GPRS is currently active (IP address assigned).
bool gprsConnected();

// Perform an HTTPS POST over GPRS using the SIM800L TCP stack.
//   host     — hostname only, e.g. "myproject-default-rtdb.firebaseio.com"
//   path     — path + query string, e.g. "/users/uid/Trip_History.json?auth=KEY"
//   body     — JSON payload
//   timeoutMs— total wait for server response (default 10s)
// Returns the HTTP status code (200 = OK), or -1 on connection failure.
int httpPostGPRS(const String& host, const String& path,
                 const String& body, unsigned long timeoutMs = 10000);

#endif