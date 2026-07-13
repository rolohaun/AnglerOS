// T-Dongle-S3 USB-OTG host printer link. Talks to the printer mainboard's USB
// CDC-ACM port the same way OctoPrint does.
#pragma once
#include <Arduino.h>

// Install the USB host stack and start watching for a CDC-ACM printer.
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
