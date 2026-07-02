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

// Init the sensor and start the stream server (skipped if the user disabled
// the camera). Returns false if no camera is running.
bool cameraBegin();

bool cameraAvailable();   // camera running right now
bool cameraSupported();   // board has camera hardware (compile-time)
bool cameraEnabled();     // user preference (persisted)

// Live start/stop: disabling stops the stream server and powers down the
// sensor (freeing its PSRAM buffers); enabling brings it all back up.
bool cameraSetEnabled(bool on);

CameraSettings cameraGetSettings();

// Clamp, apply to the live sensor (or just persist when disabled).
bool cameraApplySettings(CameraSettings s, bool persist);
