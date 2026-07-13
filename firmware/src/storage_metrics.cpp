#include "storage_metrics.h"

#include <LittleFS.h>
#include <SD_MMC.h>
#include "driver/sdmmc_types.h"

static bool s_sdMounted = false;
static const char *s_sdStatus = "Not initialized";
static const char *s_sdType = "None";

static const char *cardTypeName(uint8_t type) {
  switch (type) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC/SDXC";
    default: return "Unknown";
  }
}

void storageBegin() {
  // Native T-Dongle-S3 TF slot wiring: CLK, CMD, D0, D1, D2, D3.
  SD_MMC.setPins(12, 16, 14, 17, 21, 18);

  if (!SD_MMC.begin("/sdcard", /*mode1bit=*/false,
                    /*format_if_mount_failed=*/false,
                    SDMMC_FREQ_HIGHSPEED)) {
    s_sdMounted = false;
    s_sdStatus = "No SD card mounted";
    s_sdType = "None";
    return;
  }

  uint8_t type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    SD_MMC.end();
    s_sdMounted = false;
    s_sdStatus = "No SD card detected";
    s_sdType = "None";
    return;
  }

  s_sdMounted = true;
  s_sdStatus = "Mounted (4-bit high-speed)";
  s_sdType = cardTypeName(type);
}

bool storageSdMounted() { return s_sdMounted; }
const char *storageSdStatus() { return s_sdStatus; }
const char *storageSdType() { return s_sdType; }

// Cached capacity numbers. usedBytes()/totalBytes() walk filesystem metadata —
// LittleFS scans its blocks and FAT free-space scans on SD can take seconds —
// so they must never run inside a web request. loop() refreshes the cache.
static uint64_t s_lfsTotal = 0, s_lfsUsed = 0, s_sdTotal = 0, s_sdUsed = 0;
static uint32_t s_lastRefreshMs = 0;
static bool s_dirty = true;
static const uint32_t REFRESH_MS = 30000;

static void refreshMetrics() {
  s_lfsTotal = LittleFS.totalBytes();
  s_lfsUsed = LittleFS.usedBytes();
  s_sdTotal = s_sdMounted ? SD_MMC.totalBytes() : 0;
  s_sdUsed = s_sdMounted ? SD_MMC.usedBytes() : 0;
  s_lastRefreshMs = millis();
  s_dirty = false;
}

void storageMetricsTick() {
  if (s_dirty || millis() - s_lastRefreshMs > REFRESH_MS) refreshMetrics();
}

void storageMarkDirty() { s_dirty = true; }

uint64_t storageLittleFsTotal() { return s_lfsTotal; }
uint64_t storageLittleFsUsed() { return s_lfsUsed; }
uint64_t storageSdTotal() { return s_sdTotal; }
uint64_t storageSdUsed() { return s_sdUsed; }
