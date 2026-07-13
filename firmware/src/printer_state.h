#pragma once

#include <Arduino.h>

// Track Marlin temperature reports for the dashboard and onboard display.
void printerStateHandleLine(const String &line);
void printerStateTick();

bool printerStateTemperaturesValid();
float printerStateHotendCurrent();
float printerStateHotendTarget();
float printerStateBedCurrent();
float printerStateBedTarget();
