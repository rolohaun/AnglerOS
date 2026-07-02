// Print job engine: streams a stored G-code file to the printer over the
// active link (USB or UART) with ok-based flow control — send one command,
// wait for Marlin's "ok", send the next. Classic serial-host protocol.
#pragma once
#include <Arduino.h>

bool printJobStart(const String &name);  // false if already printing / no file
void printJobPause();
void printJobResume();
void printJobCancel();

// Call frequently from loop(): feeds the next line when the last one is acked.
void printJobPump();

// Called (from the loop task) for every line the printer sends; consumes acks.
void printJobHandlePrinterLine(const String &line);

// idle | printing | paused | done | cancelled | error
const char *printJobState();
bool printJobActive();  // printing or paused
String printJobFile();
uint8_t printJobProgress();       // 0-100 (by bytes)
uint32_t printJobElapsedSec();
uint64_t printJobBytesSent();
uint64_t printJobBytesTotal();
