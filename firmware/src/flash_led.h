// Onboard ESP32-CAM flash LED brightness control.
#pragma once
#include <Arduino.h>

void flashLedBegin();
void flashLedSetBrightness(uint8_t percent);
uint8_t flashLedBrightness();
