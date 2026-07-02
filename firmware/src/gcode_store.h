// G-code file storage: /gcode on the SD card when one is mounted, else on
// LittleFS. Uploads land here; the print job streams from here.
#pragma once
#include <Arduino.h>
#include <FS.h>

void gcodeStoreBegin();  // call after storageBegin()

// Filesystem currently backing /gcode, and which one it is.
fs::FS &gcodeFs();
bool gcodeOnSd();
uint64_t gcodeFreeBytes();

// Reduce an arbitrary upload name to a safe flat filename (no paths).
String gcodeSanitizeName(const String &raw);

File gcodeOpen(const String &name);                  // read
File gcodeCreate(const String &name);                // write (truncates)
bool gcodeDelete(const String &name);
