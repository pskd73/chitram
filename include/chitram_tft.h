#pragma once

#include <Adafruit_ILI9341.h>
#include <stdint.h>

// ILI9341 driver that mirrors every draw into a PSRAM RGB565 framebuffer.
// Needed for screenshots: this board wires SPI write-only (no TFT MISO).
//
// Adafruit text/icons use writePixel/writeFillRect (transaction API);
// images use writePixels. All must update the mirror for a consistent shot.
class ChitramTft : public Adafruit_ILI9341 {
public:
  ChitramTft(int8_t cs, int8_t dc, int8_t rst);

  // Allocate PSRAM mirror (call after begin + setRotation).
  bool beginMirror();

  const uint16_t *framebuffer() const { return fb_; }
  int16_t fbWidth() const { return fbW_; }
  int16_t fbHeight() const { return fbH_; }

  // When true, draw calls update the PSRAM mirror only (no SPI).
  // Use with blitFbRect() to compose a frame then show it in one shot.
  void setFbOnly(bool on) { fbOnly_ = on && fb_; }
  bool fbOnly() const { return fbOnly_; }
  // Copy a rectangle from the mirror to the panel.
  void blitFbRect(int16_t x, int16_t y, int16_t w, int16_t h);

  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;

  // High-level (self-contained transactions)
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                uint16_t color) override;
  void fillScreen(uint16_t color) override;
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override;
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override;

  // Transaction API used by text, bitmaps, rounded rects, etc.
  void writePixel(int16_t x, int16_t y, uint16_t color) override;
  void writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                     uint16_t color) override;
  void writeFastHLine(int16_t x, int16_t y, int16_t w,
                      uint16_t color) override;
  void writeFastVLine(int16_t x, int16_t y, int16_t h,
                      uint16_t color) override;
  void writePixels(uint16_t *colors, uint32_t len, bool block = true,
                   bool bigEndian = false);

private:
  void mirrorPixel(int16_t x, int16_t y, uint16_t color);
  void mirrorFill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

  uint16_t *fb_ = nullptr;
  int16_t fbW_ = 0;
  int16_t fbH_ = 0;
  uint16_t awX_ = 0;
  uint16_t awY_ = 0;
  uint16_t awW_ = 1;
  uint32_t awIdx_ = 0;
  bool fbOnly_ = false;
  bool skipMirror_ = false;
};
