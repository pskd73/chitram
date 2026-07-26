#pragma once

#include <FS.h>

// LittleFS on classic ESP32; onboard SD_MMC on ESP32-S3 CAM.
bool storageBegin();
fs::FS &imageFs();
