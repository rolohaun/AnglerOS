// AnglerOS — ESP32-CAM firmware entry point
//
// Phase 0 skeleton: bring up serial, mount LittleFS, start Wi-Fi (STA if
// provisioned, else SoftAP captive portal), and serve the SPA. Subsystems
// (camera, Marlin UART bridge, GitHub client, config generator) are added in
// later phases — see the project plan.

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

static const char *AP_SSID = "AnglerOS-Setup";

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("AnglerOS booting...");

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount failed");
  } else {
    Serial.println("[fs] LittleFS mounted");
  }

  // TODO(phase0): load saved Wi-Fi creds from NVS; connect as STA if present.
  // For now, always start the setup access point.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  Serial.printf("[wifi] SoftAP started: %s (%s)\n", AP_SSID,
                WiFi.softAPIP().toString().c_str());

  // TODO(phase0): start ESPAsyncWebServer, serve /data SPA, WebSocket endpoint.
}

void loop() {
  // TODO(phase0+): service web server / camera / UART bridge tasks.
  delay(1000);
}
