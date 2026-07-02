// OV3660 camera: init + MJPEG streaming server on port 81.
// Endpoints: http://<ip>:81/stream (multipart MJPEG), http://<ip>:81/capture
// (single JPEG). Boards without a camera get no-op stubs.
#pragma once
#include <Arduino.h>

// Init the sensor and start the stream server. Returns false if no camera.
bool cameraBegin();

bool cameraAvailable();
