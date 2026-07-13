// Storage initialization and capacity metrics for LittleFS + T-Dongle TF slot.
#pragma once
#include <Arduino.h>

void storageBegin();

// Capacity queries walk filesystem metadata (slow — FAT free-space scans can
// take seconds), so results are cached: the accessors below return the cache,
// refreshed from loop() every ~30s or when marked dirty (e.g. after uploads).
void storageMetricsTick();
void storageMarkDirty();

bool storageSdMounted();
const char *storageSdStatus();
const char *storageSdType();

uint64_t storageLittleFsTotal();
uint64_t storageLittleFsUsed();
uint64_t storageSdTotal();
uint64_t storageSdUsed();
