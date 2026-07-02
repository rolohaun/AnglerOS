// AnglerOS — ESP32-CAM firmware entry point
//
// Phase 0: mount LittleFS, connect to saved Wi-Fi (STA) or fall back to a
// SoftAP setup portal, and serve the SPA + status/provisioning API. First-time
// Wi-Fi setup is also handled over USB via Improv, so the ESP Web Tools
// installer can guide the user straight from "flash" to "connected". Camera,
// Marlin UART bridge, GitHub client and config generator arrive in later phases.

#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ImprovWiFiLibrary.h>

#include "settings_store.h"
#include "web_server.h"
#include "printer_uart.h"
#include "system_metrics.h"
#include "storage_metrics.h"
#include "flash_led.h"

static const char *FW_VERSION = "0.1.0-dev";
static const char *AP_SSID = "AnglerOS-Setup";
static const uint32_t STA_TIMEOUT_MS = 15000;
// Marlin's default BAUDRATE. Must match the printer serial port the ESP32 wires
// into (see docs/hardware.md); adjustable later from the UI.
static const uint32_t PRINTER_BAUD = 250000;

// Improv Wi-Fi over the USB serial link (shared with the boot log).
static ImprovWiFi improvSerial(&Serial);

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

// Fired when the browser (via Improv) successfully sets up Wi-Fi. Persist the
// credentials so the board reconnects on its own next boot.
static void onImprovConnected(const char *ssid, const char *password) {
  settingsSaveWifi(ssid, password);
  Serial.printf("[wifi] Improv connected to \"%s\": http://%s/\n", ssid,
                WiFi.localIP().toString().c_str());
  Serial.println("[setup] Done! Click \"Visit Device\" in the installer, or "
                 "open the address above in your browser.");
}

static void onImprovError(ImprovTypes::Error err) {
  Serial.printf("[wifi] Improv error: %d\n", (int)err);
}

void setup() {
  // A larger RX buffer helps the Improv handshake survive the busy boot window
  // (LittleFS mount + Wi-Fi + web server) without dropping request bytes.
  Serial.setRxBufferSize(1024);
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

  improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32, "AnglerOS",
                             FW_VERSION, "AnglerOS", "http://{LOCAL_IPV4}");
  improvSerial.onImprovConnected(onImprovConnected);
  improvSerial.onImprovError(onImprovError);

  WifiCreds creds;
  bool connected = false;
  if (settingsLoadWifi(creds)) {
    connected = connectSTA(creds);
  }

  if (connected) {
    Serial.printf("[wifi] STA connected: http://%s/\n",
                  WiFi.localIP().toString().c_str());
  } else {
    // Keep AP + STA both live: STA lets Improv provision over USB while the
    // SoftAP portal stays available as a fallback.
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);
    Serial.println("[setup] No Wi-Fi configured yet. Two ways to set it up:");
    Serial.println("[setup]  1) In the browser installer: click \"Next\", then "
                   "\"Connect to Wi-Fi\".");
    Serial.printf("[setup]  2) Or join the \"%s\" hotspot and open http://%s/\n",
                  AP_SSID, WiFi.softAPIP().toString().c_str());
  }

  printerUartBegin(PRINTER_BAUD);
  webServerBegin(FW_VERSION);
  Serial.println("[web] server started");
}

void loop() {
  systemMetricsTick();
  improvSerial.handleSerial();
  printerUartPump();

  if (webServerPendingRestart()) {
    Serial.println("[wifi] credentials saved, rebooting...");
    delay(500);
    ESP.restart();
  }
}
