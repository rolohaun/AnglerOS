#include "printer_state.h"

#include "print_job.h"
#include "printer_link.h"

static float s_hotendCurrent = 0.0f;
static float s_hotendTarget = 0.0f;
static float s_bedCurrent = 0.0f;
static float s_bedTarget = 0.0f;
static uint32_t s_lastTemperatureMs = 0;
static uint32_t s_lastPollMs = 0;

static bool parseTemperature(const String &line, const char *field,
                             float &current, float &target) {
  int start = line.indexOf(field);
  if (start < 0) return false;

  start += strlen(field);
  int slash = line.indexOf('/', start);
  if (slash < 0) return false;

  current = line.substring(start, slash).toFloat();
  target = line.substring(slash + 1).toFloat();
  return true;
}

void printerStateHandleLine(const String &line) {
  bool updated = false;
  updated |= parseTemperature(line, "T:", s_hotendCurrent, s_hotendTarget);
  updated |= parseTemperature(line, "B:", s_bedCurrent, s_bedTarget);
  if (updated) s_lastTemperatureMs = millis();
}

void printerStateTick() {
  // A print enables Marlin's M155 auto-reporting. Polling during that window
  // would inject extra acknowledgements into the print engine's flow control.
  if (printJobActive() || millis() - s_lastPollMs < 5000) return;
  s_lastPollMs = millis();
  printerLinkSend("M105");
}

bool printerStateTemperaturesValid() {
  return s_lastTemperatureMs && millis() - s_lastTemperatureMs < 30000;
}

float printerStateHotendCurrent() { return s_hotendCurrent; }
float printerStateHotendTarget() { return s_hotendTarget; }
float printerStateBedCurrent() { return s_bedCurrent; }
float printerStateBedTarget() { return s_bedTarget; }
