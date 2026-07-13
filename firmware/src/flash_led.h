// T-Dongle-S3 onboard APA102 light brightness control.
#pragma once
#include <Arduino.h>

void flashLedBegin();
void flashLedSetBrightness(uint8_t percent);
uint8_t flashLedBrightness();
