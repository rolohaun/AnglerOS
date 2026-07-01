#include "settings_store.h"
#include <Preferences.h>

static const char *NS = "angleros";

bool settingsLoadWifi(WifiCreds &out) {
  Preferences prefs;
  prefs.begin(NS, /*readOnly=*/true);
  out.ssid = prefs.getString("ssid", "");
  out.pass = prefs.getString("pass", "");
  prefs.end();
  return out.ssid.length() > 0;
}

void settingsSaveWifi(const String &ssid, const String &pass) {
  Preferences prefs;
  prefs.begin(NS, /*readOnly=*/false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}
