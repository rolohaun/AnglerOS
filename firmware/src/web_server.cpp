#include "web_server.h"
#include "settings_store.h"

#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static String s_fwVersion;
static volatile bool s_pendingRestart = false;

bool webServerPendingRestart() { return s_pendingRestart; }

static void handleStatus(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["fw"] = s_fwVersion;
  doc["chip"] = "ESP32";

  bool sta = (WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED;
  doc["mode"] = sta ? "sta" : "ap";
  doc["ip"] = sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  if (sta) {
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
  }
  doc["heap"] = ESP.getFreeHeap();
  doc["psram"] = ESP.getFreePsram();
  doc["uptime"] = (uint32_t)(millis() / 1000);

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// POST /api/wifi  (form params: ssid, pass) — save creds, then reboot into STA.
static void handleWifiSave(AsyncWebServerRequest *req) {
  if (!req->hasParam("ssid", true)) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"ssid required\"}");
    return;
  }
  String ssid = req->getParam("ssid", true)->value();
  String pass = req->hasParam("pass", true) ? req->getParam("pass", true)->value() : "";
  settingsSaveWifi(ssid, pass);
  req->send(200, "application/json", "{\"ok\":true}");
  s_pendingRestart = true;  // main loop reboots shortly after the reply flushes.
}

static void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType,
                      void *, uint8_t *, size_t) {
  // Phase 3: push temps + serial console over the socket. No-op for now.
}

void webServerBegin(const char *fwVersion) {
  s_fwVersion = fwVersion;

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);

  // Saved printer configuration (field values + generated config.ini).
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/printer.json")) {
      req->send(LittleFS, "/printer.json", "application/json");
    } else {
      req->send(200, "application/json", "{}");
    }
  });

  // Stream the POST body straight to a file so we don't buffer it in RAM.
  server.on(
      "/api/config", HTTP_POST,
      [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"ok\":true}");
      },
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        static File f;
        if (index == 0) f = LittleFS.open("/printer.json", "w");
        if (f) f.write(data, len);
        if (index + len == total && f) f.close();
      });

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *req) {
    // SPA fallback so client-side navigation still resolves.
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else {
      req->send(404, "text/plain", "AnglerOS: UI assets not found on LittleFS");
    }
  });

  server.begin();
}
