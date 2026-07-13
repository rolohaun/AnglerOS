#include "printer_usb.h"

// Minimal CDC-ACM host on ESP-IDF's usb_host stack. Marlin boards (RP2040,
// STM32) enumerate as standard CDC-ACM serial devices, so this is the same
// path OctoPrint uses — just with the ESP32-S3 as the USB host.

#include "usb/usb_host.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG_FMT = "[usb] %s\n";

// CAUTION: 1200 baud on an RP2040 CDC port triggers the UF2 bootloader reset
// convention. The line coding is virtual for USB CDC, but keep it a normal
// value regardless.
static const uint32_t CDC_BAUD = 115200;

static usb_host_client_handle_t s_client = nullptr;
static usb_device_handle_t s_dev = nullptr;
static uint8_t s_devAddr = 0;
static uint8_t s_commIfc = 0xFF;
static uint8_t s_dataIfc = 0xFF;
static uint8_t s_epIn = 0, s_epOut = 0;
static uint16_t s_epInMps = 64, s_epOutMps = 64;
static usb_transfer_t *s_inXfer = nullptr;
static usb_transfer_t *s_outXfer = nullptr;

static volatile bool s_connected = false;
static volatile bool s_outBusy = false;
static SemaphoreHandle_t s_txMutex = nullptr;
static String s_txPending;

// RX bytes cross from the USB host task to the Arduino loop task through this
// ring; lines are assembled and dispatched only from printerUsbPump().
static uint8_t s_ring[4096];
static volatile uint16_t s_head = 0, s_tail = 0;
static portMUX_TYPE s_ringMux = portMUX_INITIALIZER_UNLOCKED;

static void (*s_onLine)(const String &) = nullptr;
static String s_lineBuf;

static void ringPush(const uint8_t *data, size_t len) {
  portENTER_CRITICAL(&s_ringMux);
  for (size_t i = 0; i < len; i++) {
    uint16_t next = (uint16_t)((s_head + 1) % sizeof(s_ring));
    if (next == s_tail) break;  // full: drop the rest
    s_ring[s_head] = data[i];
    s_head = next;
  }
  portEXIT_CRITICAL(&s_ringMux);
}

static int ringPop() {
  int c = -1;
  portENTER_CRITICAL(&s_ringMux);
  if (s_tail != s_head) {
    c = s_ring[s_tail];
    s_tail = (uint16_t)((s_tail + 1) % sizeof(s_ring));
  }
  portEXIT_CRITICAL(&s_ringMux);
  return c;
}

// ---- Transfers -------------------------------------------------------------

static void inXferCb(usb_transfer_t *xfer) {
  if (xfer->status == USB_TRANSFER_STATUS_COMPLETED && xfer->actual_num_bytes > 0) {
    ringPush(xfer->data_buffer, xfer->actual_num_bytes);
  }
  if (s_connected) usb_host_transfer_submit(xfer);  // keep listening
}

static void startNextOutLocked();  // fwd

static void outXferCb(usb_transfer_t *) {
  if (s_txMutex && xSemaphoreTake(s_txMutex, 0) == pdTRUE) {
    s_outBusy = false;
    startNextOutLocked();
    xSemaphoreGive(s_txMutex);
  } else {
    s_outBusy = false;
  }
}

// Must hold s_txMutex. Sends up to one OUT packet's worth of pending bytes.
static void startNextOutLocked() {
  if (s_outBusy || !s_connected || !s_txPending.length() || !s_outXfer) return;
  size_t n = min((size_t)s_txPending.length(), (size_t)s_epOutMps);
  memcpy(s_outXfer->data_buffer, s_txPending.c_str(), n);
  s_outXfer->num_bytes = (int)n;
  s_txPending.remove(0, n);
  s_outBusy = true;
  if (usb_host_transfer_submit(s_outXfer) != ESP_OK) s_outBusy = false;
}

// Fire-and-forget control request (EP0 serializes them in submit order).
static void ctrlCb(usb_transfer_t *xfer) { usb_host_transfer_free(xfer); }

static void ctrlRequest(uint8_t bmReqType, uint8_t bReq, uint16_t wValue,
                        uint16_t wIndex, const uint8_t *data, uint16_t wLength) {
  usb_transfer_t *xfer = nullptr;
  if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &xfer) != ESP_OK) return;
  usb_setup_packet_t *setup = (usb_setup_packet_t *)xfer->data_buffer;
  setup->bmRequestType = bmReqType;
  setup->bRequest = bReq;
  setup->wValue = wValue;
  setup->wIndex = wIndex;
  setup->wLength = wLength;
  if (data && wLength) memcpy(xfer->data_buffer + sizeof(usb_setup_packet_t), data, wLength);
  xfer->num_bytes = sizeof(usb_setup_packet_t) + wLength;
  xfer->device_handle = s_dev;
  xfer->bEndpointAddress = 0;
  xfer->callback = ctrlCb;
  xfer->context = nullptr;
  if (usb_host_transfer_submit_control(s_client, xfer) != ESP_OK) usb_host_transfer_free(xfer);
}

// ---- Device discovery -------------------------------------------------------

static bool findCdcInterfaces(const usb_config_desc_t *cfg) {
  s_commIfc = s_dataIfc = 0xFF;
  s_epIn = s_epOut = 0;

  const uint8_t *p = (const uint8_t *)cfg;
  const uint8_t *end = p + cfg->wTotalLength;
  uint8_t curIfc = 0xFF, curClass = 0;

  while (p + 2 <= end && p[0] >= 2) {
    uint8_t len = p[0], type = p[1];
    if (p + len > end) break;

    if (type == 0x04 && len >= 9) {           // INTERFACE
      curIfc = p[2];
      curClass = p[5];
      if (curClass == 0x02 && s_commIfc == 0xFF) s_commIfc = curIfc;  // CDC comm
      if (curClass == 0x0A && s_dataIfc == 0xFF) s_dataIfc = curIfc;  // CDC data
    } else if (type == 0x05 && len >= 7 && curClass == 0x0A) {  // ENDPOINT on data ifc
      uint8_t addr = p[2];
      uint8_t attr = p[3] & 0x03;
      uint16_t mps = (uint16_t)(p[4] | (p[5] << 8));
      if (attr == 0x02) {  // bulk
        if (addr & 0x80) { s_epIn = addr; s_epInMps = mps; }
        else { s_epOut = addr; s_epOutMps = mps; }
      }
    }
    p += len;
  }
  return s_dataIfc != 0xFF && s_epIn && s_epOut;
}

static void attachDevice(uint8_t addr) {
  if (s_dev) return;  // one printer at a time
  if (usb_host_device_open(s_client, addr, &s_dev) != ESP_OK) return;
  s_devAddr = addr;

  const usb_config_desc_t *cfg = nullptr;
  if (usb_host_get_active_config_descriptor(s_dev, &cfg) != ESP_OK || !findCdcInterfaces(cfg)) {
    Serial.printf(TAG_FMT, "attached device is not CDC-ACM; ignoring");
    usb_host_device_close(s_client, s_dev);
    s_dev = nullptr;
    return;
  }

  if (usb_host_interface_claim(s_client, s_dev, s_dataIfc, 0) != ESP_OK) {
    usb_host_device_close(s_client, s_dev);
    s_dev = nullptr;
    return;
  }

  usb_host_transfer_alloc(s_epInMps, 0, &s_inXfer);
  s_inXfer->device_handle = s_dev;
  s_inXfer->bEndpointAddress = s_epIn;
  s_inXfer->num_bytes = s_epInMps;
  s_inXfer->callback = inXferCb;

  usb_host_transfer_alloc(s_epOutMps, 0, &s_outXfer);
  s_outXfer->device_handle = s_dev;
  s_outXfer->bEndpointAddress = s_epOut;
  s_outXfer->callback = outXferCb;

  // CDC-ACM setup on the comm interface: line coding, then raise DTR+RTS
  // (Marlin's TinyUSB CDC gates output on DTR).
  uint16_t ctlIfc = (s_commIfc != 0xFF) ? s_commIfc : s_dataIfc;
  uint8_t lineCoding[7] = {
      (uint8_t)(CDC_BAUD & 0xFF), (uint8_t)((CDC_BAUD >> 8) & 0xFF),
      (uint8_t)((CDC_BAUD >> 16) & 0xFF), (uint8_t)((CDC_BAUD >> 24) & 0xFF),
      0 /*1 stop*/, 0 /*no parity*/, 8 /*data bits*/};
  ctrlRequest(0x21, 0x20 /*SET_LINE_CODING*/, 0, ctlIfc, lineCoding, sizeof(lineCoding));
  ctrlRequest(0x21, 0x22 /*SET_CONTROL_LINE_STATE*/, 0x0003 /*DTR|RTS*/, ctlIfc, nullptr, 0);

  s_connected = true;
  usb_host_transfer_submit(s_inXfer);
  Serial.printf(TAG_FMT, "printer attached (CDC-ACM)");
}

static void detachDevice() {
  s_connected = false;
  if (s_dev) {
    if (s_dataIfc != 0xFF) usb_host_interface_release(s_client, s_dev, s_dataIfc);
    usb_host_device_close(s_client, s_dev);
  }
  if (s_inXfer) { usb_host_transfer_free(s_inXfer); s_inXfer = nullptr; }
  if (s_outXfer) { usb_host_transfer_free(s_outXfer); s_outXfer = nullptr; }
  s_dev = nullptr;
  s_devAddr = 0;
  s_dataIfc = s_commIfc = 0xFF;
  s_outBusy = false;
  Serial.printf(TAG_FMT, "printer detached");
}

static void clientEventCb(const usb_host_client_event_msg_t *msg, void *) {
  switch (msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      attachDevice(msg->new_dev.address);
      break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      if (msg->dev_gone.dev_hdl == s_dev) detachDevice();
      break;
    default:
      break;
  }
}

// ---- Tasks ------------------------------------------------------------------

static void usbLibTask(void *) {
  for (;;) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}

static void usbClientTask(void *) {
  for (;;) usb_host_client_handle_events(s_client, portMAX_DELAY);
}

// ---- Public API ---------------------------------------------------------------

void printerUsbBegin() {
  if (s_client) return;
  s_txMutex = xSemaphoreCreateMutex();
  s_lineBuf.reserve(128);

  usb_host_config_t hostCfg = {};
  hostCfg.skip_phy_setup = false;
  hostCfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  if (usb_host_install(&hostCfg) != ESP_OK) {
    Serial.printf(TAG_FMT, "usb_host_install failed");
    return;
  }

  usb_host_client_config_t clientCfg = {};
  clientCfg.is_synchronous = false;
  clientCfg.max_num_event_msg = 8;
  clientCfg.async.client_event_callback = clientEventCb;
  clientCfg.async.callback_arg = nullptr;
  if (usb_host_client_register(&clientCfg, &s_client) != ESP_OK) {
    Serial.printf(TAG_FMT, "usb_host_client_register failed");
    return;
  }

  xTaskCreatePinnedToCore(usbLibTask, "usb_lib", 4096, nullptr, 5, nullptr, 0);
  xTaskCreatePinnedToCore(usbClientTask, "usb_client", 4096, nullptr, 5, nullptr, 0);
  Serial.printf(TAG_FMT, "host started; waiting for a printer on the OTG port");
}

bool printerUsbConnected() { return s_connected; }

void printerUsbOnLine(void (*cb)(const String &)) { s_onLine = cb; }

void printerUsbSend(const String &line) {
  if (!s_connected || !s_txMutex) return;
  if (xSemaphoreTake(s_txMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
  if (s_txPending.length() < 4096) {
    s_txPending += line;
    s_txPending += '\n';
    startNextOutLocked();
  }
  xSemaphoreGive(s_txMutex);
}

void printerUsbPump() {
  int c;
  while ((c = ringPop()) >= 0) {
    if (c == '\n') {
      if (s_lineBuf.length() && s_onLine) s_onLine(s_lineBuf);
      s_lineBuf = "";
    } else if (c != '\r') {
      s_lineBuf += (char)c;
      if (s_lineBuf.length() > 512) s_lineBuf = "";  // runaway guard
    }
  }
}
