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
  doc["supported"] = cameraSupported();
  doc["enabled"] = cameraEnabled();
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
  if (!cameraSupported()) {
    req->send(503, "application/json", "{\"ok\":false,\"err\":\"no camera\"}");
    return;
  }
  auto intParam = [&](const char *name, int fallback) {
    return req->hasParam(name, true) ? req->getParam(name, true)->value().toInt() : fallback;
  };

  bool ok = true;
  if (req->hasParam("enabled", true)) {
    ok = cameraSetEnabled(intParam("enabled", 1) != 0);
  }

  CameraSettings s = cameraGetSettings();
  s.framesize = (uint8_t)intParam("framesize", s.framesize);
  s.quality = (uint8_t)intParam("quality", s.quality);
  s.fps = (uint8_t)intParam("fps", s.fps);
  s.vflip = intParam("vflip", s.vflip) != 0;
  s.hmirror = intParam("hmirror", s.hmirror) != 0;
  ok = cameraApplySettings(s, /*persist=*/true) && ok;

  req->send(ok ? 200 : 503, "application/json",
            ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"camera failed to start\"}");
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
static String s_uploadPath;
static uint8_t s_uploadFallbackBuf[16 * 1024];
static uint8_t *s_uploadBuf = s_uploadFallbackBuf;
static size_t s_uploadBufCap = sizeof(s_uploadFallbackBuf);
static size_t s_uploadBufLen = 0;

static void uploadEnsureBuffer() {
  if (s_uploadBuf != s_uploadFallbackBuf) return;

  // The ESP32-S3-CAM has 8MB PSRAM. Use it as a rolling upload window so
  // browser uploads are not throttled by tiny SD/LittleFS write buffers. Files
  // can still be much larger than this; the window flushes repeatedly.
  if (!ESP.getPsramSize()) return;

  const size_t target = 512 * 1024;
  uint8_t *buf = (uint8_t *)ps_malloc(target);
  if (buf) {
    s_uploadBuf = buf;
    s_uploadBufCap = target;
    Serial.printf("[upload] using %uKB PSRAM buffer\n", (unsigned)(s_uploadBufCap / 1024));
  }
}

static bool uploadFlush() {
  if (!s_upload || !s_uploadBufLen) return true;
  size_t expected = s_uploadBufLen;
  size_t written = s_upload.write(s_uploadBuf, s_uploadBufLen);
  s_uploadBufLen = 0;
  return written == expected;
}

static void uploadWrite(const uint8_t *data, size_t len) {
  while (!s_uploadRejected && s_upload && len) {
    size_t room = sizeof(s_uploadBuf) - s_uploadBufLen;
    size_t n = min(room, len);
    memcpy(s_uploadBuf + s_uploadBufLen, data, n);
    s_uploadBufLen += n;
    data += n;
    len -= n;

    if (s_uploadBufLen == sizeof(s_uploadBuf) && !uploadFlush()) {
      s_uploadRejected = true;
    }
  }
}

static void uploadBegin(const String &filename, size_t totalBytes) {
  s_uploadRejected = false;
  s_uploadPath = gcodeSanitizeName(filename);
  s_uploadBufLen = 0;
  uploadEnsureBuffer();

  // Reject uploads that clearly can't fit (leave ~64KB headroom).
  if (totalBytes + 65536 > gcodeFreeBytes()) {
    s_uploadRejected = true;
    return;
  }

  s_upload = gcodeCreate(s_uploadPath);
  if (!s_upload) {
    s_uploadRejected = true;
  } else {
    // The default VFS stdio buffer is only 4KB. Bigger buffering keeps SD writes
    // closer to block-sized bursts and cuts a surprising amount of overhead.
    s_upload.setBufferSize(64 * 1024);
  }
}

static void uploadEnd() {
  if (s_upload) {
    if (!uploadFlush()) s_uploadRejected = true;
    s_upload.close();
  }
  if (s_uploadRejected && s_uploadPath.length()) gcodeDelete(s_uploadPath);
  s_uploadPath = "";
  storageMarkDirty();  // refresh cached free-space numbers from loop()
}

static void handleGcodeUploadChunk(AsyncWebServerRequest *req, String filename,
                                   size_t index, uint8_t *data, size_t len, bool final) {
  if (index == 0) {
    uploadBegin(filename, req->contentLength());
  }
  uploadWrite(data, len);
  if (final) uploadEnd();
}

static void handleGcodeUploadBody(AsyncWebServerRequest *req, uint8_t *data,
                                  size_t len, size_t index, size_t total) {
  if (index == 0) {
    String filename = req->hasParam("name") ? req->getParam("name")->value() : "upload.gcode";
    uploadBegin(filename, total);
  }
  uploadWrite(data, len);
  if (index + len == total) uploadEnd();
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
  server.on("/api/gcode/upload", HTTP_POST, handleGcodeUploadDone,
            handleGcodeUploadChunk, handleGcodeUploadBody);
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
    if (ok) storageMarkDirty();
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

  // Assets ship pre-gzipped (CI runs scripts/gzip_fs.py); serveStatic finds the
  // .gz variants automatically. Short cache so reloads skip re-downloading.
  server.serveStatic("/", LittleFS, "/")
      .setDefaultFile("index.html")
      .setCacheControl("public, max-age=300");

  server.onNotFound([](AsyncWebServerRequest *req) {
    // SPA fallback so client-side navigation still resolves.
    if (LittleFS.exists("/index.html")) {
      req->send(LittleFS, "/index.html", "text/html");
    } else if (LittleFS.exists("/index.html.gz")) {
      AsyncWebServerResponse *res = req->beginResponse(LittleFS, "/index.html.gz", "text/html");
      res->addHeader("Content-Encoding", "gzip");
      req->send(res);
    } else {
      req->send(404, "text/plain", "AnglerOS: UI assets not found on LittleFS");
    }
  });

  server.begin();
}
