// AnglerOS firmware entry point for the LILYGO T-Dongle-S3.
//
// Phase 0: mount LittleFS, connect to saved Wi-Fi (STA) or fall back to a
// SoftAP setup portal, and serve the SPA + status/provisioning API. First-time
// Wi-Fi setup uses a temporary access point so the dongle's USB port remains
// available to the printer host stack.

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>

#include "settings_store.h"
#include "web_server.h"
#include "printer_link.h"
#include "system_metrics.h"
#include "storage_metrics.h"
#include "flash_led.h"
#include "gcode_store.h"
#include "print_job.h"

static const char *FW_VERSION = "0.1.0-dev";
static const char *AP_SSID = "AnglerOS-Setup";
static const uint32_t STA_TIMEOUT_MS = 15000;
// Marlin's default BAUDRATE. Must match the printer serial port the T-Dongle wires
// into (see docs/hardware.md); adjustable later from the UI.
static const uint32_t PRINTER_BAUD = 250000;

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
  systemMetricsBegin();
  flashLedBegin();
  delay(200);
  Serial.printf("\nAnglerOS %s booting...\n", FW_VERSION);

  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount failed");
  } else {
    Serial.println("[fs] LittleFS mounted");
  }

  storageBegin();
  if (storageSdMounted()) {
    Serial.printf("[sd] %s %llu / %llu bytes used\n", storageSdType(),
                  (unsigned long long)storageSdUsed(),
                  (unsigned long long)storageSdTotal());
  } else {
    Serial.printf("[sd] %s\n", storageSdStatus());
  }
  gcodeStoreBegin();

  WifiCreds creds;
  bool connected = false;
  if (settingsLoadWifi(creds)) {
    connected = connectSTA(creds);
  }

  if (connected) {
    Serial.printf("[wifi] STA connected: http://%s/\n",
                  WiFi.localIP().toString().c_str());
  } else {
    // Keep AP + STA live so the setup form can scan and join a network.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);
    Serial.printf("[setup] Join \"%s\" and open http://%s/\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  // Modem sleep adds 100-300ms latency to every web request — the #1 cause of
  // a sluggish UI. Costs some power; this board is mains-fed.
  WiFi.setSleep(false);

  printerLinkBegin(PRINTER_BAUD);
  webServerBegin(FW_VERSION);
  Serial.println("[web] server started");
}

void loop() {
  systemMetricsTick();
  storageMetricsTick();
  printerLinkPump();
  printJobPump();

  if (webServerPendingRestart()) {
    Serial.println("[wifi] credentials saved, rebooting...");
    delay(500);
    ESP.restart();
  }

  // Briefly yield to core 1's scheduler; this still services the print job far
  // faster than Marlin consumes lines.
  delay(2);
}
