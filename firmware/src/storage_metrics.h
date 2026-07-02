// Storage initialization and capacity metrics for LittleFS + ESP32-CAM SD slot.
#pragma once
#include <Arduino.h>

void storageBegin();

bool storageSdMounted();
const char *storageSdStatus();
const char *storageSdType();

uint64_t storageLittleFsTotal();
uint64_t storageLittleFsUsed();
uint64_t storageSdTotal();
uint64_t storageSdUsed();
