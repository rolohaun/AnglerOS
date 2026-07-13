// Unified T-Dongle-S3 printer link. USB-OTG is preferred when attached; the
// QWIIC UART remains available as a wiring-friendly fallback.
#pragma once
#include <Arduino.h>

void printerLinkBegin(uint32_t uartBaud);

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
