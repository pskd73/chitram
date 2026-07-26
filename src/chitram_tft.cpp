#include "chitram_tft.h"

#include <esp_heap_caps.h>
#include <string.h>

ChitramTft::ChitramTft(int8_t cs, int8_t dc, int8_t rst)
    : Adafruit_ILI9341(cs, dc, rst) {}

bool ChitramTft::beginMirror() {
  fbW_ = width();
  fbH_ = height();
  if (fbW_ < 1 || fbH_ < 1) {
    return false;
  }
  size_t bytes = (size_t)fbW_ * (size_t)fbH_ * sizeof(uint16_t);
  if (fb_) {
    heap_caps_free(fb_);
    fb_ = nullptr;
  }
  fb_ = (uint16_t *)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!fb_) {
    fb_ = (uint16_t *)malloc(bytes);
  }
  if (!fb_) {
    fbW_ = 0;
    fbH_ = 0;
    return false;
  }
  memset(fb_, 0, bytes);
  return true;
}

void ChitramTft::mirrorPixel(int16_t x, int16_t y, uint16_t color) {
  if (!fb_ || x < 0 || y < 0 || x >= fbW_ || y >= fbH_) {
    return;
  }
  fb_[(size_t)y * (size_t)fbW_ + (size_t)x] = color;
}

void ChitramTft::mirrorFill(int16_t x, int16_t y, int16_t w, int16_t h,
                            uint16_t color) {
  if (!fb_ || w < 1 || h < 1) {
    return;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > fbW_) {
    w = fbW_ - x;
  }
  if (y + h > fbH_) {
    h = fbH_ - y;
  }
  if (w < 1 || h < 1) {
    return;
  }
  for (int16_t row = 0; row < h; ++row) {
    uint16_t *p = fb_ + (size_t)(y + row) * (size_t)fbW_ + (size_t)x;
    for (int16_t col = 0; col < w; ++col) {
      p[col] = color;
    }
  }
}

void ChitramTft::setAddrWindow(uint16_t x, uint16_t y, uint16_t w,
                               uint16_t h) {
  Adafruit_ILI9341::setAddrWindow(x, y, w, h);
  awX_ = x;
  awY_ = y;
  awW_ = w ? w : 1;
  awIdx_ = 0;
}

void ChitramTft::drawPixel(int16_t x, int16_t y, uint16_t color) {
  // SPITFT::drawPixel writes SPI directly (not via writePixel).
  Adafruit_ILI9341::drawPixel(x, y, color);
  mirrorPixel(x, y, color);
}

void ChitramTft::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                          uint16_t color) {
  // SPITFT::fillRect uses writeFillRectPreclipped → writeColor (not virtual).
  Adafruit_ILI9341::fillRect(x, y, w, h, color);
  mirrorFill(x, y, w, h, color);
}

void ChitramTft::fillScreen(uint16_t color) {
  Adafruit_ILI9341::fillScreen(color);
  if (fb_) {
    size_t n = (size_t)fbW_ * (size_t)fbH_;
    for (size_t i = 0; i < n; ++i) {
      fb_[i] = color;
    }
  }
}

void ChitramTft::drawFastHLine(int16_t x, int16_t y, int16_t w,
                               uint16_t color) {
  Adafruit_ILI9341::drawFastHLine(x, y, w, color);
  mirrorFill(x, y, w, 1, color);
}

void ChitramTft::drawFastVLine(int16_t x, int16_t y, int16_t h,
                               uint16_t color) {
  Adafruit_ILI9341::drawFastVLine(x, y, h, color);
  mirrorFill(x, y, 1, h, color);
}

void ChitramTft::writePixel(int16_t x, int16_t y, uint16_t color) {
  Adafruit_ILI9341::writePixel(x, y, color);
  mirrorPixel(x, y, color);
}

void ChitramTft::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               uint16_t color) {
  Adafruit_ILI9341::writeFillRect(x, y, w, h, color);
  mirrorFill(x, y, w, h, color);
}

void ChitramTft::writeFastHLine(int16_t x, int16_t y, int16_t w,
                                uint16_t color) {
  Adafruit_ILI9341::writeFastHLine(x, y, w, color);
  mirrorFill(x, y, w, 1, color);
}

void ChitramTft::writeFastVLine(int16_t x, int16_t y, int16_t h,
                                uint16_t color) {
  Adafruit_ILI9341::writeFastVLine(x, y, h, color);
  mirrorFill(x, y, 1, h, color);
}

void ChitramTft::writePixels(uint16_t *colors, uint32_t len, bool block,
                             bool bigEndian) {
  Adafruit_ILI9341::writePixels(colors, len, block, bigEndian);
  if (!fb_ || !colors || len == 0 || awW_ == 0) {
    return;
  }
  for (uint32_t i = 0; i < len; ++i) {
    uint16_t px = colors[i];
    if (bigEndian) {
      px = (uint16_t)((px << 8) | (px >> 8));
    }
    uint16_t x = (uint16_t)(awX_ + (awIdx_ % awW_));
    uint16_t y = (uint16_t)(awY_ + (awIdx_ / awW_));
    ++awIdx_;
    if (x < (uint16_t)fbW_ && y < (uint16_t)fbH_) {
      fb_[(size_t)y * (size_t)fbW_ + (size_t)x] = px;
    }
  }
}
