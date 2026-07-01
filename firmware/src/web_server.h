// Async web server: serves the SPA from LittleFS, exposes a status API and a
// Wi-Fi provisioning endpoint. WebSocket endpoint is stubbed for Phase 3.
#pragma once
#include <Arduino.h>

void webServerBegin(const char *fwVersion);

// True after a successful Wi-Fi provisioning POST, signalling main to reboot.
bool webServerPendingRestart();
