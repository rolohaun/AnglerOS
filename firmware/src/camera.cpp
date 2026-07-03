#include "camera.h"

#if defined(ANGLEROS_BOARD_CAM) || defined(ANGLEROS_BOARD_S3CAM)

#include "esp_camera.h"
#include "esp_http_server.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// The MJPEG stream runs on its own tiny HTTP server (port 81) because
// multipart streaming doesn't fit ESPAsyncWebServer's response model.
static const uint16_t STREAM_PORT = 81;

#if defined(ANGLEROS_BOARD_CAM)
// AI-Thinker ESP32-CAM pin map.
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27
#define CAM_PIN_Y9 35
#define CAM_PIN_Y8 34
#define CAM_PIN_Y7 39
#define CAM_PIN_Y6 36
#define CAM_PIN_Y5 21
#define CAM_PIN_Y4 19
#define CAM_PIN_Y3 18
#define CAM_PIN_Y2 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22
#else
// ESP32-S3-CAM pin map (confirmed against the vendor pinout).
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 15
#define CAM_PIN_SIOD 4
#define CAM_PIN_SIOC 5
#define CAM_PIN_Y9 16
#define CAM_PIN_Y8 17
#define CAM_PIN_Y7 18
#define CAM_PIN_Y6 12
#define CAM_PIN_Y5 10
#define CAM_PIN_Y4 8
#define CAM_PIN_Y3 9
#define CAM_PIN_Y2 11
#define CAM_PIN_VSYNC 6
#define CAM_PIN_HREF 7
#define CAM_PIN_PCLK 13
#endif

static bool s_cameraOk = false;
static bool s_psram = false;
static httpd_handle_t s_httpd = nullptr;

// Defaults: VGA at sharp JPEG quality (q12) with a 10 fps cap — ~300KB/s,
// within the ESP32's comfortable Wi-Fi TX budget. Frame buffers are allocated
// at SVGA so the user can go up to SVGA at runtime without re-init.
static CameraSettings s_settings = {8 /*VGA*/, 12, 10, true, false};
static bool s_enabled = false;  // user preference, persisted
static const char *SETTINGS_PATH = "/camera.json";

static void loadSettings() {
  File f = LittleFS.open(SETTINGS_PATH, "r");
  if (!f) return;
  JsonDocument doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    s_settings.framesize = doc["framesize"] | s_settings.framesize;
    s_settings.quality = doc["quality"] | s_settings.quality;
    s_settings.fps = doc["fps"] | s_settings.fps;
    s_settings.vflip = doc["vflip"] | s_settings.vflip;
    s_settings.hmirror = doc["hmirror"] | s_settings.hmirror;
    s_enabled = doc["enabled"] | s_enabled;
  }
  f.close();
}

static void saveSettings() {
  File f = LittleFS.open(SETTINGS_PATH, "w");
  if (!f) return;
  JsonDocument doc;
  doc["framesize"] = s_settings.framesize;
  doc["quality"] = s_settings.quality;
  doc["fps"] = s_settings.fps;
  doc["vflip"] = s_settings.vflip;
  doc["hmirror"] = s_settings.hmirror;
  doc["enabled"] = s_enabled;
  serializeJson(doc, f);
  f.close();
}

CameraSettings cameraGetSettings() { return s_settings; }

bool cameraApplySettings(CameraSettings s, bool persist) {
  // Clamp: buffers are allocated at SVGA (QVGA without PSRAM).
  uint8_t maxSize = s_psram ? 9 /*SVGA*/ : 5 /*QVGA*/;
  s.framesize = constrain(s.framesize, (uint8_t)5, maxSize);
  s.quality = constrain(s.quality, (uint8_t)10, (uint8_t)40);
  if (s.fps > 30) s.fps = 30;

  if (s_cameraOk) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (!sensor) return false;
    sensor->set_framesize(sensor, (framesize_t)s.framesize);
    sensor->set_quality(sensor, s.quality);
    sensor->set_vflip(sensor, s.vflip ? 1 : 0);
    sensor->set_hmirror(sensor, s.hmirror ? 1 : 0);
  }
  // When the camera is disabled, values are stored and applied on next enable.

  s_settings = s;
  if (persist) saveSettings();
  return true;
}

static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t streamHandler(httpd_req_t *req) {
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part[64];
  uint32_t lastFrame = 0;
  for (;;) {
    // fps cap: bound Wi-Fi bandwidth so the stream can't starve the web UI.
    if (s_settings.fps) {
      uint32_t interval = 1000 / s_settings.fps;
      uint32_t since = millis() - lastFrame;
      if (since < interval) vTaskDelay(pdMS_TO_TICKS(interval - since));
    }
    lastFrame = millis();

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      res = ESP_FAIL;
      break;
    }
    size_t hlen = snprintf(part, sizeof(part), STREAM_PART, fb->len);
    if (httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY)) != ESP_OK ||
        httpd_resp_send_chunk(req, part, hlen) != ESP_OK ||
        httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len) != ESP_OK) {
      esp_camera_fb_return(fb);
      break;  // client went away
    }
    esp_camera_fb_return(fb);
  }
  return res;
}

static esp_err_t captureHandler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static void startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = STREAM_PORT;
  config.ctrl_port = 32769;      // keep clear of other httpd instances
  config.max_open_sockets = 3;   // stream viewers are few
  config.lru_purge_enable = true;
  config.core_id = 0;            // keep stream/network work off the print loop
  // Run below the async web server's task so streaming can't make the UI lag.
  config.task_priority = 2;

  if (httpd_start(&s_httpd, &config) != ESP_OK) {
    Serial.println("[cam] stream server failed to start");
    return;
  }
  httpd_uri_t stream = {.uri = "/stream", .method = HTTP_GET, .handler = streamHandler, .user_ctx = nullptr};
  httpd_uri_t capture = {.uri = "/capture", .method = HTTP_GET, .handler = captureHandler, .user_ctx = nullptr};
  httpd_register_uri_handler(s_httpd, &stream);
  httpd_register_uri_handler(s_httpd, &capture);
  Serial.printf("[cam] MJPEG stream on :%u/stream\n", STREAM_PORT);
}

static bool startCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_PIN_Y2;
  config.pin_d1 = CAM_PIN_Y3;
  config.pin_d2 = CAM_PIN_Y4;
  config.pin_d3 = CAM_PIN_Y5;
  config.pin_d4 = CAM_PIN_Y6;
  config.pin_d5 = CAM_PIN_Y7;
  config.pin_d6 = CAM_PIN_Y8;
  config.pin_d7 = CAM_PIN_Y9;
  config.pin_xclk = CAM_PIN_XCLK;
  config.pin_pclk = CAM_PIN_PCLK;
  config.pin_vsync = CAM_PIN_VSYNC;
  config.pin_href = CAM_PIN_HREF;
  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;
  config.pin_pwdn = CAM_PIN_PWDN;
  config.pin_reset = CAM_PIN_RESET;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  s_psram = psramFound();
  if (s_psram) {
    // Allocate for SVGA so runtime settings can range QVGA..SVGA.
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 14;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[cam] init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_brightness(s, 1);
    s->set_saturation(s, -1);
  }

  s_cameraOk = true;
  cameraApplySettings(s_settings, false);  // clamp + apply persisted prefs
  startStreamServer();
  return true;
}

static void stopCamera() {
  if (s_httpd) {
    httpd_stop(s_httpd);
    s_httpd = nullptr;
  }
  if (s_cameraOk) {
    esp_camera_deinit();  // frees the PSRAM frame buffers
    s_cameraOk = false;
  }
}

bool cameraBegin() {
  loadSettings();
  if (!s_enabled) {
    Serial.println("[cam] disabled by user setting");
    return false;
  }
  return startCamera();
}

bool cameraAvailable() { return s_cameraOk; }
bool cameraSupported() { return true; }
bool cameraEnabled() { return s_enabled; }

bool cameraSetEnabled(bool on) {
  if (on == s_enabled && on == s_cameraOk) {
    s_enabled = on;
    saveSettings();
    return true;
  }
  s_enabled = on;
  saveSettings();
  if (on) return startCamera();
  stopCamera();
  return true;
}

#else  // boards without a camera

bool cameraBegin() { return false; }
bool cameraAvailable() { return false; }
bool cameraSupported() { return false; }
bool cameraEnabled() { return false; }
bool cameraSetEnabled(bool) { return false; }
CameraSettings cameraGetSettings() { return CameraSettings{}; }
bool cameraApplySettings(CameraSettings, bool) { return false; }

#endif
