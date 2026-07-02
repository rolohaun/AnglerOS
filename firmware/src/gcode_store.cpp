#include "gcode_store.h"
#include "storage_metrics.h"

#include <LittleFS.h>
#include <SD_MMC.h>

static bool s_onSd = false;
static const char *DIR = "/gcode";

void gcodeStoreBegin() {
  s_onSd = storageSdMounted();
  fs::FS &fs = gcodeFs();
  if (!fs.exists(DIR)) fs.mkdir(DIR);
  Serial.printf("[gcode] storing files on %s\n", s_onSd ? "SD card" : "LittleFS");
}

fs::FS &gcodeFs() { return s_onSd ? (fs::FS &)SD_MMC : (fs::FS &)LittleFS; }

bool gcodeOnSd() { return s_onSd; }

uint64_t gcodeFreeBytes() {
  if (s_onSd) return storageSdTotal() - storageSdUsed();
  return storageLittleFsTotal() - storageLittleFsUsed();
}

String gcodeSanitizeName(const String &raw) {
  // Strip any path components, then keep a conservative character set.
  String name = raw;
  int slash = max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
  if (slash >= 0) name = name.substring(slash + 1);

  String out;
  out.reserve(name.length());
  for (size_t i = 0; i < name.length() && out.length() < 48; i++) {
    char c = name[i];
    if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') out += c;
    else if (c == ' ') out += '_';
  }
  if (!out.length()) out = "upload.gcode";
  return out;
}

static String pathFor(const String &name) { return String(DIR) + "/" + gcodeSanitizeName(name); }

File gcodeOpen(const String &name) { return gcodeFs().open(pathFor(name), "r"); }

File gcodeCreate(const String &name) { return gcodeFs().open(pathFor(name), "w"); }

bool gcodeDelete(const String &name) { return gcodeFs().remove(pathFor(name)); }
