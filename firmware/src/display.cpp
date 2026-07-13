#include "display.h"

#include <Adafruit_GFX.h>
#include <SPI.h>
#include <WiFi.h>

#include "print_job.h"
#include "printer_link.h"
#include "printer_state.h"

static const int8_t TFT_MOSI = 3;
static const int8_t TFT_SCLK = 5;
static const int8_t TFT_CS = 4;
static const int8_t TFT_DC = 2;
static const int8_t TFT_RST = 1;
static const int8_t TFT_BACKLIGHT = 38;

static const uint16_t SCREEN_WIDTH = 160;
static const uint16_t SCREEN_HEIGHT = 80;
static const uint16_t COLOR_BLACK = 0x0000;
static const uint16_t COLOR_GREEN = 0x07E0;
static const uint16_t COLOR_WHITE = 0xFFFF;
static const uint16_t COLOR_YELLOW = 0xFFE0;
static const uint16_t COLOR_MUTED = 0x7BEF;

static const uint32_t TFT_SPI_HZ = 40000000;
static const uint8_t TFT_X_GAP = 1;
static const uint8_t TFT_Y_GAP = 26;

static GFXcanvas16 s_canvas(SCREEN_WIDTH, SCREEN_HEIGHT);
static bool s_ready = false;
static uint32_t s_lastRefreshMs = 0;

static void panelCommand(uint8_t command, const uint8_t *data = nullptr,
                         size_t length = 0) {
  SPI.beginTransaction(SPISettings(TFT_SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(TFT_CS, LOW);
  digitalWrite(TFT_DC, LOW);
  SPI.write(command);
  if (length) {
    digitalWrite(TFT_DC, HIGH);
    SPI.writeBytes(data, length);
  }
  digitalWrite(TFT_CS, HIGH);
  SPI.endTransaction();
}

static void panelInit() {
  // T-Dongle-S3 ST7735 sequence from LILYGO's board support package.
  // This panel's power and gamma setup differs from generic 160x80 modules.
  static const uint8_t frameRate[] = {0x05, 0x3A, 0x3A};
  static const uint8_t partialFrameRate[] = {0x05, 0x3A, 0x3A,
                                              0x05, 0x3A, 0x3A};
  static const uint8_t power1[] = {0x62, 0x02, 0x04};
  static const uint8_t power3[] = {0x0D, 0x00};
  static const uint8_t power4[] = {0x8D, 0x6A};
  static const uint8_t power5[] = {0x8D, 0xEE};
  static const uint8_t gammaPositive[] = {
      0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07,
      0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10};
  static const uint8_t gammaNegative[] = {
      0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08,
      0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10};

  panelCommand(0x01);  // Software reset
  delay(150);
  panelCommand(0x11);  // Sleep out
  delay(120);
  panelCommand(0xB1, frameRate, sizeof(frameRate));
  panelCommand(0xB2, frameRate, sizeof(frameRate));
  panelCommand(0xB3, partialFrameRate, sizeof(partialFrameRate));
  const uint8_t inversionControl = 0x03;
  panelCommand(0xB4, &inversionControl, 1);
  panelCommand(0xC0, power1, sizeof(power1));
  const uint8_t power2 = 0xC0;
  panelCommand(0xC1, &power2, 1);
  panelCommand(0xC2, power3, sizeof(power3));
  panelCommand(0xC3, power4, sizeof(power4));
  panelCommand(0xC4, power5, sizeof(power5));
  const uint8_t vcom = 0x0E;
  panelCommand(0xC5, &vcom, 1);
  panelCommand(0x21);  // Display inversion on
  const uint8_t colorMode = 0x05;
  panelCommand(0x3A, &colorMode, 1);
  panelCommand(0xE0, gammaPositive, sizeof(gammaPositive));
  panelCommand(0xE1, gammaNegative, sizeof(gammaNegative));

  // Landscape, BGR color order, mirrored vertically to match the enclosure.
  const uint8_t memoryAccess = 0xA8;
  panelCommand(0x36, &memoryAccess, 1);
  panelCommand(0x13);  // Normal display on
  delay(10);
  panelCommand(0x29);  // Main display on
  delay(100);
}

static void flushCanvas() {
  const uint16_t xEnd = TFT_X_GAP + SCREEN_WIDTH - 1;
  const uint16_t yEnd = TFT_Y_GAP + SCREEN_HEIGHT - 1;
  const uint8_t columns[] = {0x00, TFT_X_GAP,
                             static_cast<uint8_t>(xEnd >> 8),
                             static_cast<uint8_t>(xEnd)};
  const uint8_t rows[] = {0x00, TFT_Y_GAP,
                          static_cast<uint8_t>(yEnd >> 8),
                          static_cast<uint8_t>(yEnd)};
  panelCommand(0x2A, columns, sizeof(columns));
  panelCommand(0x2B, rows, sizeof(rows));

  // GFX stores RGB565 in host byte order; ST7735 expects the high byte first.
  s_canvas.byteSwap();
  panelCommand(0x2C, reinterpret_cast<const uint8_t *>(s_canvas.getBuffer()),
               SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
  s_canvas.byteSwap();
}

static String fitLine(const String &value) {
  if (value.length() <= 26) return value;
  return value.substring(0, 25) + "~";
}

static void drawRow(uint8_t row, const String &value, uint16_t color) {
  const int16_t y = 1 + row * 11;
  s_canvas.setCursor(2, y + 1);
  s_canvas.setTextColor(color, COLOR_BLACK);
  s_canvas.print(fitLine(value));
}

static String temperatureLine() {
  if (!printerStateTemperaturesValid()) return "H --/--  B --/--";

  char line[27];
  snprintf(line, sizeof(line), "H %d/%d  B %d/%d",
           (int)lroundf(printerStateHotendCurrent()),
           (int)lroundf(printerStateHotendTarget()),
           (int)lroundf(printerStateBedCurrent()),
           (int)lroundf(printerStateBedTarget()));
  return String(line);
}

static String jobStateLine() {
  String state = printJobState();
  state.toUpperCase();
  if (printJobActive() || state == "DONE") {
    state += " ";
    state += printJobProgress();
    state += "%";
  }
  return state;
}

static void drawProgress() {
  int16_t progress = printJobActive() || String(printJobState()) == "done"
                         ? printJobProgress()
                         : 0;

  const int16_t x = 3;
  const int16_t y = 69;
  const int16_t width = 154;
  s_canvas.drawRect(x, y, width, 9, COLOR_MUTED);
  if (progress > 0) {
    int16_t fill = ((width - 2) * progress) / 100;
    s_canvas.fillRect(x + 1, y + 1, fill, 7, COLOR_GREEN);
  }
}

void displayBegin() {
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);  // active-low: prevent a white boot flash

  pinMode(TFT_CS, OUTPUT);
  pinMode(TFT_DC, OUTPUT);
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(120);
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  panelInit();

  s_canvas.setTextWrap(false);
  s_canvas.setTextSize(1);
  s_canvas.fillScreen(COLOR_BLACK);
  s_canvas.setCursor(2, 2);
  s_canvas.setTextColor(COLOR_GREEN, COLOR_BLACK);
  s_canvas.print("AnglerOS starting...");
  flushCanvas();

  digitalWrite(TFT_BACKLIGHT, LOW);
  s_ready = true;
}

void displayTick() {
  if (!s_ready || millis() - s_lastRefreshMs < 750) return;
  s_lastRefreshMs = millis();

  String ip = WiFi.status() == WL_CONNECTED
                  ? "IP " + WiFi.localIP().toString()
                  : "SETUP " + WiFi.softAPIP().toString();
  String file = printJobActive() || String(printJobState()) == "done"
                    ? "F " + printJobFile()
                    : "No active print";
  String command = printJobActive() ? "> " + printJobCurrentCommand() : "";
  String link = "LINK ";
  link += printerLinkActive();
  link.toUpperCase();

  s_canvas.fillScreen(COLOR_BLACK);
  drawRow(0, ip, COLOR_GREEN);
  drawRow(1, temperatureLine(), COLOR_YELLOW);
  drawRow(2, jobStateLine(), COLOR_WHITE);
  drawRow(3, file, COLOR_WHITE);
  drawRow(4, command, COLOR_MUTED);
  drawRow(5, link, COLOR_MUTED);
  drawProgress();
  flushCanvas();
}
