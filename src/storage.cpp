#include "storage.h"
#include "config.h"

#if defined(CHITRAM_BOARD_S3)
#include <SD_MMC.h>

static bool storageReady = false;

bool storageBegin() {
  if (storageReady) {
    return true;
  }
  SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
  if (!SD_MMC.begin("/sdcard", true /*1-bit*/, false, SDMMC_FREQ_DEFAULT, 5)) {
    Serial.println("ERR SD_MMC mount failed — insert FAT32 card");
    return false;
  }
  if (SD_MMC.cardType() == CARD_NONE) {
    Serial.println("ERR SD card not detected");
    return false;
  }
  storageReady = true;
  Serial.printf("SD ok type=%u size=%lluMB freeheap=%u\n",
                (unsigned)SD_MMC.cardType(),
                (unsigned long long)(SD_MMC.cardSize() / (1024ULL * 1024ULL)),
                (unsigned)ESP.getFreeHeap());
  return true;
}

fs::FS &imageFs() { return SD_MMC; }

#else
#include <LittleFS.h>

static bool storageReady = false;

bool storageBegin() {
  if (storageReady) {
    return true;
  }
  if (!LittleFS.begin(true)) {
    Serial.println("WARN LittleFS mount failed — image gen needs flash FS");
    return false;
  }
  storageReady = true;
  Serial.printf("LittleFS ok total=%u used=%u\n",
                (unsigned)LittleFS.totalBytes(),
                (unsigned)LittleFS.usedBytes());
  return true;
}

fs::FS &imageFs() { return LittleFS; }

#endif
