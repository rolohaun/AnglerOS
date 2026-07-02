#include "flash_led.h"

static uint8_t s_brightness = 0;

#if defined(ANGLEROS_BOARD_CAM)
// ESP32-CAM: big white flash LED on GPIO4.
static const int8_t FLASH_LED_PIN = 4;
#elif defined(ANGLEROS_BOARD_S3CAM)
// ESP32-S3-CAM: onboard LED on GPIO2 ("LED ON" in the vendor pinout; PWM
// capable, not a strapping pin on the S3). A WS2812 RGB also sits on GPIO48
// (future status light).
static const int8_t FLASH_LED_PIN = 2;
#else
static const int8_t FLASH_LED_PIN = -1;
#endif

static const uint8_t FLASH_LED_CHANNEL = 2;
static const uint16_t FLASH_LED_FREQ = 5000;
static const uint8_t FLASH_LED_RESOLUTION = 8;

void flashLedBegin() {
  if (FLASH_LED_PIN < 0) return;
  ledcSetup(FLASH_LED_CHANNEL, FLASH_LED_FREQ, FLASH_LED_RESOLUTION);
  ledcAttachPin(FLASH_LED_PIN, FLASH_LED_CHANNEL);
  flashLedSetBrightness(0);
}

void flashLedSetBrightness(uint8_t percent) {
  s_brightness = min<uint8_t>(percent, 100);
  if (FLASH_LED_PIN < 0) return;
  uint32_t duty = map(s_brightness, 0, 100, 0, 255);
  ledcWrite(FLASH_LED_CHANNEL, duty);
}

uint8_t flashLedBrightness() { return s_brightness; }
