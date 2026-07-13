// T-Dongle-S3 QWIIC UART bridge to the printer mainboard (Marlin serial).
#pragma once
#include <Arduino.h>

void printerUartBegin(uint32_t baud);
bool printerUartAvailable();

// Call frequently from loop(): reads complete lines and dispatches them.
void printerUartPump();

// Register a handler for each line received from the printer.
void printerUartOnLine(void (*cb)(const String &));

// Send one G-code line to the printer (newline is appended).
void printerSend(const String &line);
