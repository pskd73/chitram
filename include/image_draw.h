#pragma once

#include <Arduino.h>

bool drawImageFile(const char *path);
// Scale image into rect. cover=false letterboxes (fit); cover=true fills and crops.
// Respects active uiClip (won't paint into the title bar).
bool drawImageInRect(const char *path, int16_t x, int16_t y, int16_t w,
                     int16_t h, bool cover = false);

// Decode cover-scaled image into dst (w*h RGB565). For gallery thumb cache.
bool loadImageCoverToBuffer(const char *path, uint16_t *dst, int w, int h);

// Blit RGB565 buffer; respects uiClip.
void blitRgb565(int16_t x, int16_t y, int16_t w, int16_t h,
                const uint16_t *rgb565);
