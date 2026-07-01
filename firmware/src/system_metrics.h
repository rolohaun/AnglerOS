#pragma once
#include <Arduino.h>

void systemMetricsBegin();
void systemMetricsTick();
uint8_t systemCpuLoadPercent();
