#include "display.h"

#include <Adafruit_ST7735.h>
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
static const uint16_t COLOR_GREEN = ST77XX_GREEN;
static const uint16_t COLOR_WHITE = ST77XX_WHITE;
static const uint16_t COLOR_YELLOW = ST77XX_YELLOW;
static const uint16_t COLOR_MUTED = 0x7BEF;

// Software SPI lets this display use LILYGO's fixed, non-default SPI pins
// without touching the SDMMC peripheral. Change-only rendering keeps traffic
// low enough that it does not disturb the print loop.
static Adafruit_ST7735 s_tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
static bool s_ready = false;
static uint32_t s_lastRefreshMs = 0;
static String s_lastRows[6];
static uint16_t s_lastColors[6] = {};
static int16_t s_lastProgress = -1;

static String fitLine(const String &value) {
  if (value.length() <= 26) return value;
  return value.substring(0, 25) + "~";
}

static void drawRow(uint8_t row, const String &value, uint16_t color) {
  String text = fitLine(value);
  if (s_lastRows[row] == text && s_lastColors[row] == color) return;

  const int16_t y = 1 + row * 11;
  s_tft.fillRect(0, y, SCREEN_WIDTH, 10, ST77XX_BLACK);
  s_tft.setCursor(2, y + 1);
  s_tft.setTextColor(color, ST77XX_BLACK);
  s_tft.print(text);
  s_lastRows[row] = text;
  s_lastColors[row] = color;
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
  if (progress == s_lastProgress) return;

  const int16_t x = 3;
  const int16_t y = 69;
  const int16_t width = 154;
  s_tft.drawRect(x, y, width, 9, COLOR_MUTED);
  s_tft.fillRect(x + 1, y + 1, width - 2, 7, ST77XX_BLACK);
  if (progress > 0) {
    int16_t fill = ((width - 2) * progress) / 100;
    s_tft.fillRect(x + 1, y + 1, fill, 7, COLOR_GREEN);
  }
  s_lastProgress = progress;
}

void displayBegin() {
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);  // active-low: prevent a white boot flash

  // The plugin variant supplies the T-Dongle's 1/26 landscape offsets and
  // inverted-color initialization.
  s_tft.initR(INITR_MINI160x80_PLUGIN);
  s_tft.setRotation(1);
  s_tft.setTextWrap(false);
  s_tft.setTextSize(1);
  s_tft.fillScreen(ST77XX_BLACK);
  s_tft.setCursor(2, 2);
  s_tft.setTextColor(COLOR_GREEN, ST77XX_BLACK);
  s_tft.print("AnglerOS starting...");

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

  drawRow(0, ip, COLOR_GREEN);
  drawRow(1, temperatureLine(), COLOR_YELLOW);
  drawRow(2, jobStateLine(), COLOR_WHITE);
  drawRow(3, file, COLOR_WHITE);
  drawRow(4, command, COLOR_MUTED);
  drawRow(5, link, COLOR_MUTED);
  drawProgress();
}
