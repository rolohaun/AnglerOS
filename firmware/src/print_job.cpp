#include "print_job.h"
#include "gcode_store.h"
#include "printer_link.h"

enum class JobState { Idle, Printing, Paused, Done, Cancelled, Error };

static JobState s_state = JobState::Idle;
static File s_file;
static String s_name;
static String s_currentCommand;
static uint64_t s_total = 0, s_read = 0;
static uint32_t s_startMs = 0, s_endMs = 0;

// ok-window of 1: how many sent commands still await an "ok".
static int s_pending = 0;
static uint32_t s_lastRxMs = 0;

// Long heats (M190) and probes answer with "busy" keepalives, so the stall
// timeout only trips when the printer goes completely silent.
static const uint32_t STALL_TIMEOUT_MS = 5UL * 60UL * 1000UL;

static void sendCmd(const String &cmd) {
  printerLinkSend(cmd);
  s_pending++;
  s_lastRxMs = millis();
}

bool printJobStart(const String &name) {
  if (s_state == JobState::Printing || s_state == JobState::Paused) return false;
  File f = gcodeOpen(name);
  if (!f || f.isDirectory()) return false;

  s_file = f;
  s_name = gcodeSanitizeName(name);
  s_total = f.size();
  s_read = 0;
  s_currentCommand = "";
  s_pending = 0;
  s_startMs = millis();
  s_endMs = 0;
  s_state = JobState::Printing;

  // Switch Marlin to auto temperature reports for the duration of the job:
  // the UI stops polling M105 while printing so stray "ok" replies can't
  // corrupt the ack pacing.
  sendCmd("M155 S5");
  return true;
}

void printJobPause() {
  if (s_state == JobState::Printing) s_state = JobState::Paused;
}

void printJobResume() {
  if (s_state == JobState::Paused) {
    s_lastRxMs = millis();
    s_state = JobState::Printing;
  }
}

static void finish(JobState end) {
  if (s_file) s_file.close();
  s_state = end;
  s_endMs = millis();
  s_pending = 0;
  s_currentCommand = "";
}

void printJobCancel() {
  if (s_state != JobState::Printing && s_state != JobState::Paused) return;
  finish(JobState::Cancelled);
  // Break any blocking wait, then make the machine safe.
  printerLinkSend("M108");
  printerLinkSend("M104 S0");
  printerLinkSend("M140 S0");
  printerLinkSend("M107");
  printerLinkSend("M155 S0");
}

void printJobHandlePrinterLine(const String &line) {
  if (s_state != JobState::Printing && s_state != JobState::Paused) return;
  s_lastRxMs = millis();  // any traffic proves the printer is alive
  if (line.startsWith("ok")) {
    if (s_pending > 0) s_pending--;
  }
}

void printJobPump() {
  if (s_state != JobState::Printing) return;

  if (s_pending > 0) {
    if (millis() - s_lastRxMs > STALL_TIMEOUT_MS) {
      finish(JobState::Error);
      Serial.println("[print] no response from printer; job stopped");
    }
    return;
  }

  // Acked and ready: read the next meaningful line (bounded comment skip so a
  // large header block can't stall the loop task).
  for (int guard = 0; guard < 200; guard++) {
    if (!s_file.available()) {
      finish(JobState::Done);
      printerLinkSend("M155 S0");
      Serial.printf("[print] done: %s\n", s_name.c_str());
      return;
    }

    String line = s_file.readStringUntil('\n');
    s_read += line.length() + 1;

    int semi = line.indexOf(';');
    if (semi >= 0) line = line.substring(0, semi);
    line.trim();
    if (!line.length() || line.length() > 200) continue;

    s_currentCommand = line;
    sendCmd(line);
    return;
  }
}

const char *printJobState() {
  switch (s_state) {
    case JobState::Printing: return "printing";
    case JobState::Paused: return "paused";
    case JobState::Done: return "done";
    case JobState::Cancelled: return "cancelled";
    case JobState::Error: return "error";
    default: return "idle";
  }
}

bool printJobActive() {
  return s_state == JobState::Printing || s_state == JobState::Paused;
}

String printJobFile() { return s_name; }
String printJobCurrentCommand() { return s_currentCommand; }

uint8_t printJobProgress() {
  if (!s_total) return 0;
  uint64_t pct = (s_read * 100) / s_total;
  return (uint8_t)min(pct, (uint64_t)100);
}

uint32_t printJobElapsedSec() {
  if (!s_startMs) return 0;
  uint32_t end = printJobActive() ? millis() : (s_endMs ? s_endMs : millis());
  return (end - s_startMs) / 1000;
}

uint64_t printJobBytesSent() { return s_read; }
uint64_t printJobBytesTotal() { return s_total; }
