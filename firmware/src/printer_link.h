// Unified printer link. Routes G-code to whichever transport is available:
// USB-OTG host (ESP32-S3, OctoPrint-style) when a printer is attached, else
// the hardware UART (ESP32-CAM wiring). The web UI doesn't care which.
#pragma once
#include <Arduino.h>

// startUsbHost: on the S3 the OTG port is shared with the setup/Improv USB
// console, so the host stack is only started once Wi-Fi is provisioned.
void printerLinkBegin(uint32_t uartBaud, bool startUsbHost);

// Call frequently from loop(): pumps both transports and dispatches lines.
void printerLinkPump();

// Register a handler for each line received from the printer.
void printerLinkOnLine(void (*cb)(const String &));

// Send one G-code line to the printer (newline appended by the transport).
void printerLinkSend(const String &line);

// True if any transport is up (UART begun or USB printer attached).
bool printerLinkAvailable();

// "usb" | "uart" | "none" — the transport currently carrying traffic.
const char *printerLinkActive();
