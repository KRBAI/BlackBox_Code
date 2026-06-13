#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

// SettingsManager is now a thin read-only wrapper over the hardcoded
// values in Config.h.  NVS flash storage and the web settings UI have
// been removed — all credentials are compiled in via Config.h.

#include <Arduino.h>
#include "Config.h"

struct DeviceSettings {
  String routerSSID;
  String routerPass;
  String phoneNumber;
  String firebaseURL;
  String firebaseAPIKey;
  String userID;
  String googleMapsAPIKey;
  String simAPN;
};

class SettingsManager {
public:
  void begin() {
    _s.routerSSID     = ROUTER_SSID;
    _s.routerPass     = ROUTER_PASS;
    _s.phoneNumber    = PHONE_NUMBER;
    _s.firebaseURL    = FIREBASE_URL;
    _s.firebaseAPIKey = FIREBASE_API_KEY;
    _s.userID         = USER_ID;
    _s.googleMapsAPIKey = "";
    _s.simAPN         = SIM_APN;
    Serial.println("[Settings] Loaded from Config.h (hardcoded).");
  }
  DeviceSettings& get() { return _s; }

private:
  DeviceSettings _s;
};

extern SettingsManager settingsManager;

#endif