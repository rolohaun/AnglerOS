#include "flash_led.h"

static uint8_t s_brightness = 0;

#if defined(ANGLEROS_BOARD_CAM)

static const int8_t FLASH_LED_PIN = 4;
static const uint8_t FLASH_LED_CHANNEL = 2;
static const uint16_t FLASH_LED_FREQ = 5000;
static const uint8_t FLASH_LED_RESOLUTION = 8;

void flashLedBegin() {
  ledcSetup(FLASH_LED_CHANNEL, FLASH_LED_FREQ, FLASH_LED_RESOLUTION);
  ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);
  flashLedSetBrightness(0);
}

void flashLedSetBrightness(uint8_t percent) {
  s_brightness = min<uint8_t>(percent, 100);
  uint32_t duty = map(s_brightness, 0, 100, 0, 255);
  ledcWrite(FLASH_LED_CHANNEL, duty);
}

#else
// ESP32-S3-CAM: GPIO4 is the camera SIOD line, and this board's flash-LED pin
// (if present) is unconfirmed — keep the API but drive nothing. TODO: wire up
// once the pin is verified on real hardware.

void flashLedBegin() {}

void flashLedSetBrightness(uint8_t percent) {
  s_brightness = min<uint8_t>(percent, 100);
}

#endif

uint8_t flashLedBrightness() { return s_brightness; }
