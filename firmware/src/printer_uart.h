// UART bridge to the printer mainboard (Marlin over serial).
// The ESP32-CAM talks to the SKR Pico on a spare hardware UART; the web UI
// streams G-code to it and its responses back over a WebSocket.
#pragma once
#include <Arduino.h>

// Start the link. RX/TX pins are chosen to avoid the ESP32-CAM strapping pins
// (see docs/hardware.md).
void printerUartBegin(uint32_t baud);
bool printerUartAvailable();

// Call frequently from loop(): reads complete lines and dispatches them.
void printerUartPump();

// Register a handler for each line received from the printer.
void printerUartOnLine(void (*cb)(const String &));

// Send one G-code line to the printer (newline is appended).
void printerSend(const String &line);
