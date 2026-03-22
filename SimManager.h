#ifndef SIMMANAGER_H
#define SIMMANAGER_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "Config.h"

struct SimStatus {
  bool    registered   = false;  // true = home or roaming network
  int     rssi         = 0;      // 0-31 raw CSQ value (99 = unknown)
  int     dBm          = 0;      // dBm = -113 + (rssi * 2)
  int     bars         = 0;      // 0-5 visual bars for display
  String  operatorName = "---";  // e.g. "Mobitel"
  String  lastRTCSync  = "Never";// timestamp of last successful RTC sync
};

// Initialize SIM800L on UART2, wait for network registration
void initSimManager();

// Refresh signal strength, operator name, and registration status.
// Call from main loop every ~10s — fast, non-blocking (~3s total AT time).
void updateSimStatus();

// Read the last-known status without triggering any AT commands
SimStatus& getSimStatus();

// Sync the DS3231 RTC from network time.
// Called automatically on boot after registration.
// Returns true on success.
bool syncTimeFromSIM();

// Send SOS SMS with a Google Maps link to the configured phone number
void sendLocationSMS(float lat, float lon);

#endif