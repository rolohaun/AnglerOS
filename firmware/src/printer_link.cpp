#include "printer_link.h"
#include "printer_uart.h"
#include "printer_usb.h"
#include "print_job.h"

static void (*s_cb)(const String &) = nullptr;

static void onLineFromTransport(const String &line) {
  printJobHandlePrinterLine(line);  // consume acks for the streaming job
  if (s_cb) s_cb(line);
}

void printerLinkBegin(uint32_t uartBaud, bool startUsbHost) {
  printerUartOnLine(&onLineFromTransport);
  printerUartBegin(uartBaud);

  if (startUsbHost) {
    printerUsbOnLine(&onLineFromTransport);
    printerUsbBegin();
  }
}

void printerLinkPump() {
  printerUartPump();
  printerUsbPump();
}

void printerLinkOnLine(void (*cb)(const String &)) { s_cb = cb; }

void printerLinkSend(const String &line) {
  // Prefer USB when a printer is actually attached; UART otherwise.
  if (printerUsbConnected()) {
    printerUsbSend(line);
  } else {
    printerSend(line);
  }
}

bool printerLinkAvailable() {
  return printerUsbConnected() || printerUartAvailable();
}

const char *printerLinkActive() {
  if (printerUsbConnected()) return "usb";
  if (printerUartAvailable()) return "uart";
  return "none";
}
