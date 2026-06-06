#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

struct DeviceSettings {
  String routerSSID;
  String routerPass;
  String phoneNumber;
  String firebaseURL;
  String firebaseAPIKey;
  String userID;
  String googleMapsAPIKey;
  String simAPN;        // Mobile operator APN  e.g. "dialogbb" / "mobitelbb" / "internet"
};

class SettingsManager {
public:
  void begin();
  void save(DeviceSettings& s);
  DeviceSettings& get();

private:
  DeviceSettings _settings;
  Preferences    _prefs;
};

extern SettingsManager settingsManager;

#endif