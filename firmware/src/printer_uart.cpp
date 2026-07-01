#include "printer_uart.h"

// ESP32-CAM UART to the printer. GPIO14 (TX) / GPIO13 (RX) are free once the SD
// card is unused and are not boot strapping pins. Common GND with the board.
static const int8_t PIN_RX = 13;
static const int8_t PIN_TX = 14;

static HardwareSerial &PSER = Serial1;
static void (*s_onLine)(const String &) = nullptr;
static String s_buf;

void printerUartBegin(uint32_t baud) {
  PSER.begin(baud, SERIAL_8N1, PIN_RX, PIN_TX);
  s_buf.reserve(128);
}

void printerUartOnLine(void (*cb)(const String &)) { s_onLine = cb; }

void printerSend(const String &line) {
  PSER.print(line);
  PSER.print('\n');
}

void printerUartPump() {
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
