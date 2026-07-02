// OV3660 camera: init + MJPEG streaming server on port 81.
// Endpoints: http://<ip>:81/stream (multipart MJPEG), http://<ip>:81/capture
// (single JPEG). Boards without a camera get no-op stubs.
#pragma once
#include <Arduino.h>

// Adjustable at runtime from the System tab; persisted to /camera.json.
struct CameraSettings {
  uint8_t framesize;  // esp_camera framesize_t value, QVGA(5)..SVGA(9)
  uint8_t quality;    // JPEG quality 10 (best) .. 40 (smallest)
  uint8_t fps;        // stream fps cap; 0 = uncapped
  bool vflip;
  bool hmirror;
};

// Init the sensor and start the stream server. Returns false if no camera.
bool cameraBegin();

bool cameraAvailable();

CameraSettings cameraGetSettings();

// Clamp, apply to the live sensor, and (optionally) persist.
bool cameraApplySettings(CameraSettings s, bool persist);
