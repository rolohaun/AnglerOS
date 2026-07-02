#include "printer_uart.h"

// ESP32-CAM UART to the printer. RX stays on GPIO13; TX uses GPIO12 so SD_MMC
// one-bit mode can keep GPIO14 for the SD clock and GPIO4 can keep the flash LED off.
static const int8_t PIN_RX = 13;
static const int8_t PIN_TX = 12;

static HardwareSerial &PSER = Serial1;
static void (*s_onLine)(const String &) = nullptr;
static String s_buf;
static bool s_started = false;

void printerUartBegin(uint32_t baud) {
  PSER.begin(baud, SERIAL_8N1, PIN_RX, PIN_TX);
  s_buf.reserve(128);
  s_started = true;
}

bool printerUartAvailable() { return s_started; }

void printerUartOnLine(void (*cb)(const String &)) { s_onLine = cb; }

void printerSend(const String &line) {
  if (!s_started) return;
  PSER.print(line);
  PSER.print('\n');
}

void printerUartPump() {
  if (!s_started) return;
  while (PSER.available()) {
    char c = (char)PSER.read();
    if (c == '\n') {
      if (s_buf.length() && s_onLine) s_onLine(s_buf);
      s_buf = "";
    } else if (c != '\r') {
      s_buf += c;
      if (s_buf.length() > 512) s_buf = "";  // runaway guard
    }
  }
}
