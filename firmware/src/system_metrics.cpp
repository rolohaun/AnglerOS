#include "system_metrics.h"

static uint32_t s_lastWindowMs = 0;
static uint32_t s_loopTicks = 0;
static uint32_t s_peakTicks = 1;
static uint8_t s_cpuLoad = 0;

void systemMetricsBegin() {
  s_lastWindowMs = millis();
  s_loopTicks = 0;
  s_peakTicks = 1;
  s_cpuLoad = 0;
}

void systemMetricsTick() {
  s_loopTicks++;
  uint32_t now = millis();
  if (now - s_lastWindowMs < 1000) return;

  uint32_t ticks = s_loopTicks;
  s_loopTicks = 0;
  s_lastWindowMs = now;

  if (ticks > s_peakTicks) s_peakTicks = ticks;
  uint32_t idleRatio = (ticks * 100UL) / s_peakTicks;
  if (idleRatio > 100) idleRatio = 100;
  s_cpuLoad = (uint8_t)(100 - idleRatio);
}

uint8_t systemCpuLoadPercent() { return s_cpuLoad; }
