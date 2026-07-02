#include "storage_metrics.h"

#include <LittleFS.h>
#include <SD_MMC.h>

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
#if defined(ANGLEROS_BOARD_S3CAM)
  // ESP32-S3-CAM TF slot: CLK=39, CMD=38, D0=40 (1-bit). The S3's SD_MMC pins
  // are muxable and must be set before begin().
  SD_MMC.setPins(39, 38, 40);
#endif
  // ESP32-CAM SD_MMC one-bit mode uses GPIO14/15/2. GPIO14 is also the default
  // printer UART TX, so main.cpp skips that UART when a card is mounted.
  if (!SD_MMC.begin("/sdcard", /*mode1bit=*/true)) {
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
  s_sdStatus = "Mounted";
  s_sdType = cardTypeName(type);
}

bool storageSdMounted() { return s_sdMounted; }
const char *storageSdStatus() { return s_sdStatus; }
const char *storageSdType() { return s_sdType; }

uint64_t storageLittleFsTotal() { return LittleFS.totalBytes(); }
uint64_t storageLittleFsUsed() { return LittleFS.usedBytes(); }
uint64_t storageSdTotal() { return s_sdMounted ? SD_MMC.totalBytes() : 0; }
uint64_t storageSdUsed() { return s_sdMounted ? SD_MMC.usedBytes() : 0; }
