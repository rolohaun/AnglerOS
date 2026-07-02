// USB-OTG host printer link (ESP32-S3 only): talks to the printer mainboard's
// USB port as a CDC-ACM host — the same way OctoPrint connects over USB.
// On chips without USB-OTG these are no-op stubs.
#pragma once
#include <Arduino.h>

// Install the USB host stack and start watching for a CDC-ACM printer.
// On the ESP32-S3-CAM the OTG USB-C is a dedicated port (console/Improv live
// on the CH340 port), so this can be started at boot.
void printerUsbBegin();

// True once a CDC-ACM device is attached, claimed, and configured.
bool printerUsbConnected();

// Register a handler for each complete line received from the printer.
void printerUsbOnLine(void (*cb)(const String &));

// Queue one G-code line for the printer (newline appended).
void printerUsbSend(const String &line);

// Call frequently from loop(): drains received bytes into lines and
// dispatches them on the caller's task (not the USB host task).
void printerUsbPump();
