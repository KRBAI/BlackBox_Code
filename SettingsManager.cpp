#include "SettingsManager.h"
#include "Config.h"

SettingsManager settingsManager;
#define NVS_NS "bb_settings"

void SettingsManager::begin() {
  _prefs.begin(NVS_NS, true);
  _settings.routerSSID       = _prefs.getString("routerSSID",     ROUTER_SSID);
  _settings.routerPass       = _prefs.getString("routerPass",     ROUTER_PASS);
  _settings.phoneNumber      = _prefs.getString("phoneNumber",    "+94727238727");
  _settings.firebaseURL      = _prefs.getString("firebaseURL",    FIREBASE_URL);
  _settings.firebaseAPIKey   = _prefs.getString("firebaseAPIKey", FIREBASE_API_KEY);
  _settings.userID           = _prefs.getString("userID",         USER_ID);
  _settings.googleMapsAPIKey = _prefs.getString("googleMapsKey",  "");
  _settings.simAPN           = _prefs.getString("simAPN",         "");
  _prefs.end();

  Serial.println("[Settings] Loaded from NVS.");
  Serial.println("[Settings] Router SSID : " + _settings.routerSSID);
  Serial.println("[Settings] Firebase URL: " + _settings.firebaseURL);
  Serial.println("[Settings] User ID     : " + _settings.userID);
}

void SettingsManager::save(DeviceSettings& s) {
  _prefs.begin(NVS_NS, false);
  _prefs.putString("routerSSID",     s.routerSSID);
  _prefs.putString("routerPass",     s.routerPass);
  _prefs.putString("phoneNumber",    s.phoneNumber);
  _prefs.putString("firebaseURL",    s.firebaseURL);
  _prefs.putString("firebaseAPIKey", s.firebaseAPIKey);
  _prefs.putString("userID",         s.userID);
  _prefs.putString("googleMapsKey",  s.googleMapsAPIKey);
  _prefs.putString("simAPN",         s.simAPN);
  _prefs.end();
  _settings = s;
  Serial.println("[Settings] Saved to NVS.");
}

DeviceSettings& SettingsManager::get() {
  return _settings;
}