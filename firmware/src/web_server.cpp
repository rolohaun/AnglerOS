#include "web_server.h"
#include "settings_store.h"
#include "printer_link.h"
#include "system_metrics.h"
#include "storage_metrics.h"
#include "flash_led.h"
#include "camera.h"
#include "gcode_store.h"
#include "print_job.h"

#ifndef ANGLEROS_BOARD_NAME
#define ANGLEROS_BOARD_NAME "ESP32"
#endif

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
  doc["chip"] = ANGLEROS_BOARD_NAME;

  bool sta = (WiFi.getMode() & WIFI_MODE_STA) && WiFi.status() == WL_CONNECTED;
  doc["mode"] = sta ? "sta" : "ap";
  doc["ip"] = sta ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  if (sta) {
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
  }
  doc["heap"] = ESP.getFreeHeap();
  doc["heap_total"] = ESP.getHeapSize();
  doc["psram"] = ESP.getFreePsram();
  doc["psram_total"] = ESP.getPsramSize();
  doc["cpu_load"] = systemCpuLoadPercent();
  doc["cpu_freq_mhz"] = getCpuFrequencyMhz();
  doc["uptime"] = (uint32_t)(millis() / 1000);
  doc["printer_uart"] = printerLinkAvailable();
  doc["printer_link"] = printerLinkActive();
  doc["fs_used"] = storageLittleFsUsed();
  doc["fs_total"] = storageLittleFsTotal();
  doc["sd_mounted"] = storageSdMounted();
  doc["sd_status"] = storageSdStatus();
  doc["sd_type"] = storageSdType();
  doc["sd_used"] = storageSdUsed();
  doc["sd_total"] = storageSdTotal();
  doc["flash_brightness"] = flashLedBrightness();
  doc["camera"] = cameraAvailable();

  JsonObject pj = doc["print"].to<JsonObject>();
  pj["state"] = printJobState();
  pj["file"] = printJobFile();
  pj["progress"] = printJobProgress();
  pj["elapsed"] = printJobElapsedSec();
  pj["bytes_sent"] = printJobBytesSent();
  pj["bytes_total"] = printJobBytesTotal();
  pj["storage"] = gcodeOnSd() ? "sd" : "littlefs";

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

// ---- Camera settings ---------------------------------------------------------

static void handleCameraGet(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["available"] = cameraAvailable();
  CameraSettings s = cameraGetSettings();
  doc["framesize"] = s.framesize;
  doc["quality"] = s.quality;
  doc["fps"] = s.fps;
  doc["vflip"] = s.vflip;
  doc["hmirror"] = s.hmirror;
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

static void handleCameraSet(AsyncWebServerRequest *req) {
  CameraSettings s = cameraGetSettings();
  auto intParam = [&](const char *name, int fallback) {
    return req->hasParam(name, true) ? req->getParam(name, true)->value().toInt() : fallback;
  };
  s.framesize = (uint8_t)intParam("framesize", s.framesize);
  s.quality = (uint8_t)intParam("quality", s.quality);
  s.fps = (uint8_t)intParam("fps", s.fps);
  s.vflip = intParam("vflip", s.vflip) != 0;
  s.hmirror = intParam("hmirror", s.hmirror) != 0;

  bool ok = cameraApplySettings(s, /*persist=*/true);
  req->send(ok ? 200 : 503, "application/json",
            ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"no camera\"}");
}

// ---- G-code files + print control ------------------------------------------

static void handleGcodeList(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["storage"] = gcodeOnSd() ? "sd" : "littlefs";
  doc["free"] = gcodeFreeBytes();
  JsonArray files = doc["files"].to<JsonArray>();

  File dir = gcodeFs().open("/gcode");
  if (dir && dir.isDirectory()) {
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        JsonObject o = files.add<JsonObject>();
        String name = f.name();
        int slash = name.lastIndexOf('/');
        o["name"] = slash >= 0 ? name.substring(slash + 1) : name;
        o["size"] = (uint32_t)f.size();
      }
      f.close();
    }
  }

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

static File s_upload;
static bool s_uploadRejected = false;

static void handleGcodeUploadChunk(AsyncWebServerRequest *req, String filename,
                                   size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    s_uploadRejected = false;
    // Reject uploads that clearly can't fit (leave ~64KB headroom).
    if (req->contentLength() + 65536 > gcodeFreeBytes()) {
      s_uploadRejected = true;
    } else {
      s_upload = gcodeCreate(filename);
      if (!s_upload) s_uploadRejected = true;
    }
  }
  if (!s_uploadRejected && s_upload) s_upload.write(data, len);
  if (final && s_upload) s_upload.close();
}

static void handleGcodeUploadDone(AsyncWebServerRequest *req) {
  if (s_uploadRejected) {
    req->send(507, "application/json", "{\"ok\":false,\"err\":\"not enough storage\"}");
  } else {
    req->send(200, "application/json", "{\"ok\":true}");
  }
}

static void handlePrintControl(AsyncWebServerRequest *req) {
  String action = req->url().substring(req->url().lastIndexOf('/') + 1);
  bool ok = true;
  const char *err = nullptr;

  if (action == "start") {
    if (!req->hasParam("name", true) && !req->hasParam("name")) {
      ok = false; err = "name required";
    } else {
      String name = req->hasParam("name", true) ? req->getParam("name", true)->value()
                                                : req->getParam("name")->value();
      if (!printerLinkAvailable() || String(printerLinkActive()) == "none") {
        ok = false; err = "printer not connected";
      } else if (!printJobStart(name)) {
        ok = false; err = "could not start (busy or file missing)";
      }
    }
  } else if (action == "pause") {
    printJobPause();
  } else if (action == "resume") {
    printJobResume();
  } else if (action == "cancel") {
    printJobCancel();
  } else {
    ok = false; err = "unknown action";
  }

  JsonDocument doc;
  doc["ok"] = ok;
  if (err) doc["err"] = err;
  doc["state"] = printJobState();
  String out;
  serializeJson(doc, out);
  req->send(ok ? 200 : 409, "application/json", out);
}

static void handleFlashStatus(AsyncWebServerRequest *req) {
  JsonDocument doc;
  doc["brightness"] = flashLedBrightness();

  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

static void handleFlashSave(AsyncWebServerRequest *req) {
  if (!req->hasParam("brightness", true)) {
    req->send(400, "application/json", "{\"ok\":false,\"err\":\"brightness required\"}");
    return;
  }
  int value = req->getParam("brightness", true)->value().toInt();
  flashLedSetBrightness((uint8_t)max(0, min(100, value)));
  req->send(200, "application/json", "{\"ok\":true}");
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

// A line arrived from the printer — fan it out to every connected browser.
static void broadcastPrinterLine(const String &line) { ws.textAll(line); }

// A browser sent a G-code line — forward it to the printer. (The browser echoes
// its own sent commands locally, so ws.textAll is only ever called from loop(),
// avoiding a cross-task race on the socket.)
static void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *, AwsEventType type,
                      void *arg, uint8_t *data, size_t len) {
  if (type != WS_EVT_DATA) return;
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len)) return;
  if (info->opcode != WS_TEXT) return;

  String msg;
  msg.reserve(len);
  for (size_t i = 0; i < len; i++) msg += (char)data[i];
  msg.trim();
  if (msg.length()) printerLinkSend(msg);
}

void webServerBegin(const char *fwVersion) {
  s_fwVersion = fwVersion;

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  printerLinkOnLine(&broadcastPrinterLine);

  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/wifi", HTTP_POST, handleWifiSave);
  server.on("/api/flash", HTTP_GET, handleFlashStatus);
  server.on("/api/flash", HTTP_POST, handleFlashSave);

  server.on("/api/camera", HTTP_GET, handleCameraGet);
  server.on("/api/camera", HTTP_POST, handleCameraSet);

  // G-code file management + print control.
  server.on("/api/gcode/list", HTTP_GET, handleGcodeList);
  server.on("/api/gcode/upload", HTTP_POST, handleGcodeUploadDone, handleGcodeUploadChunk);
  server.on("/api/gcode/delete", HTTP_POST, [](AsyncWebServerRequest *req) {
    if (!req->hasParam("name", true)) {
      req->send(400, "application/json", "{\"ok\":false,\"err\":\"name required\"}");
      return;
    }
    String name = req->getParam("name", true)->value();
    if (printJobActive() && gcodeSanitizeName(name) == printJobFile()) {
      req->send(409, "application/json", "{\"ok\":false,\"err\":\"file is printing\"}");
      return;
    }
    bool ok = gcodeDelete(name);
    req->send(ok ? 200 : 404, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"not found\"}");
  });
  server.on("/api/print/start", HTTP_POST, handlePrintControl);
  server.on("/api/print/pause", HTTP_POST, handlePrintControl);
  server.on("/api/print/resume", HTTP_POST, handlePrintControl);
  server.on("/api/print/cancel", HTTP_POST, handlePrintControl);

  // Saved UI settings, such as the display name shown in the sidebar.
  server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/settings.json")) {
      req->send(LittleFS, "/settings.json", "application/json");
    } else {
      req->send(200, "application/json", "{}");
    }
  });
  server.on(
      "/api/settings", HTTP_POST,
      [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); },
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        static File f;
        if (index == 0) f = LittleFS.open("/settings.json", "w");
        if (f) f.write(data, len);
        if (index + len == total && f) f.close();
      });

  // Saved macros (array of { name, gcode }).
  server.on("/api/macros", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (LittleFS.exists("/macros.json")) {
      req->send(LittleFS, "/macros.json", "application/json");
    } else {
      req->send(200, "application/json", "[]");
    }
  });
  server.on(
      "/api/macros", HTTP_POST,
      [](AsyncWebServerRequest *req) { req->send(200, "application/json", "{\"ok\":true}"); },
      nullptr,
      [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index,
         size_t total) {
        static File f;
        if (index == 0) f = LittleFS.open("/macros.json", "w");
        if (f) f.write(data, len);
        if (index + len == total && f) f.close();
      });

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
