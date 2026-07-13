#include "printer_uart.h"
#include "driver/gpio.h"

// T-Dongle-S3 QWIIC header. The board labels these pins RX/TX; cross them at
// the printer so the dongle's TX reaches the printer's RX and vice versa.
static const int8_t PIN_RX = 44;
static const int8_t PIN_TX = 43;

static HardwareSerial &PSER = Serial1;
static void (*s_onLine)(const String &) = nullptr;
static String s_buf;
static bool s_started = false;

void printerUartBegin(uint32_t baud) {
  PSER.begin(baud, SERIAL_8N1, PIN_RX, PIN_TX);
  // Idle the RX line high when nothing is wired to it — a floating pin picks
  // up noise that turns into junk "printer output" broadcast to the browser.
  gpio_set_pull_mode((gpio_num_t)PIN_RX, GPIO_PULLUP_ONLY);
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
    } else if (c >= 32 && c < 127) {  // printable ASCII only: drop line noise
      s_buf += c;
      if (s_buf.length() > 512) s_buf = "";  // runaway guard
    }
  }
}
