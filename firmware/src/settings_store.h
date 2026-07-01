// Persistent settings in NVS (Wi-Fi creds now; PAT + printer config later).
#pragma once
#include <Arduino.h>

struct WifiCreds {
  String ssid;
  String pass;
};

// Returns true if a non-empty SSID was stored.
bool settingsLoadWifi(WifiCreds &out);
void settingsSaveWifi(const String &ssid, const String &pass);
