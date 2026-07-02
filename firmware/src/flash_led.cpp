#include "flash_led.h"

static const int8_t FLASH_LED_PIN = 4;
static const uint8_t FLASH_LED_CHANNEL = 2;
static const uint16_t FLASH_LED_FREQ = 5000;
static const uint8_t FLASH_LED_RESOLUTION = 8;

static uint8_t s_brightness = 0;

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

uint8_t flashLedBrightness() { return s_brightness; }
