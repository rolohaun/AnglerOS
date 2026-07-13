#include "flash_led.h"

static uint8_t s_brightness = 0;
static const uint8_t LED_DATA_PIN = 40;
static const uint8_t LED_CLOCK_PIN = 39;

static void writeByte(uint8_t value) {
  for (int8_t bit = 7; bit >= 0; --bit) {
    digitalWrite(LED_DATA_PIN, (value >> bit) & 1);
    digitalWrite(LED_CLOCK_PIN, HIGH);
    digitalWrite(LED_CLOCK_PIN, LOW);
  }
}

static void showWhite(uint8_t level) {
  for (uint8_t i = 0; i < 4; ++i) writeByte(0x00);  // start frame
  writeByte(0xFF);  // LED frame marker + full 5-bit global brightness
  writeByte(level); // blue
  writeByte(level); // green
  writeByte(level); // red
  for (uint8_t i = 0; i < 4; ++i) writeByte(0xFF);  // end frame
}

void flashLedBegin() {
  pinMode(LED_DATA_PIN, OUTPUT);
  pinMode(LED_CLOCK_PIN, OUTPUT);
  digitalWrite(LED_DATA_PIN, LOW);
  digitalWrite(LED_CLOCK_PIN, LOW);
  flashLedSetBrightness(0);
}

void flashLedSetBrightness(uint8_t percent) {
  s_brightness = min<uint8_t>(percent, 100);
  showWhite((uint8_t)map(s_brightness, 0, 100, 0, 255));
}

uint8_t flashLedBrightness() { return s_brightness; }
