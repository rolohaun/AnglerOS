// AnglerOS — ESP32-CAM firmware entry point
//
// Phase 0: mount LittleFS, connect to saved Wi-Fi (STA) or fall back to a
// SoftAP setup portal, and serve the SPA + status/provisioning API. Camera,
// Marlin UART bridge, GitHub client and config generator arrive in later
// phases — see the project plan.

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "settings_store.h"
#include "web_server.h"

static const char *FW_VERSION = "0.1.0-dev";
static const char *AP_SSID = "AnglerOS-Setup";
static const uint32_t STA_TIMEOUT_MS = 15000;

static bool connectSTA(const WifiCreds &c) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(c.ssid.c_str(), c.pass.c_str());
  Serial.printf("[wifi] connecting to \"%s\"", c.ssid.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < STA_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\nAnglerOS %s booting...\n", FW_VERSION);

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount failed");
  } else {
    Serial.println("[fs] LittleFS mounted");
  }

  WifiCreds creds;
  bool connected = false;
  if (settingsLoadWifi(creds)) {
    connected = connectSTA(creds);
  }

  if (connected) {
    Serial.printf("[wifi] STA connected: http://%s/\n",
                  WiFi.localIP().toString().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    Serial.printf("[wifi] setup portal: SSID \"%s\" -> http://%s/\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str());
  }

  webServerBegin(FW_VERSION);
  Serial.println("[web] server started");
}

void loop() {
  if (webServerPendingRestart()) {
    Serial.println("[wifi] credentials saved, rebooting...");
    delay(500);
    ESP.restart();
  }
  delay(100);
}
